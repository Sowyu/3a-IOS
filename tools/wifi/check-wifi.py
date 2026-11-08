#!/usr/bin/env python3
"""Host checks for the Wi-Fi patches (2240 to 2252), on the source the patches produce.

  python3 -I check-wifi.py TREE MODEM_A_IMAGE

TREE is an xnu checkout whose tag wifi-base is the daily tree the series applies to (research/xnu-wifi-k5/dxb/xnu).
The patches are applied to wifi-base in a scratch copy under /tmp, the result must equal TREE's working files, and the
host model (tests/wpss.c) is compiled with sanitizers on that copy and run with MODEM_A_IMAGE read only. Then each
negative control mutates one safeguard and must make the model fail at that safeguard. Writes only a new /tmp/wifi-check-*
directory and deletes nothing."""
import os, shutil, subprocess as sp, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCHES = sorted((HERE / '../../patches/wifi').resolve().glob('224*.patch')) + \
          sorted((HERE / '../../patches/wifi').resolve().glob('225*.patch'))
FILES = ['bsd/dev/battery/AdspFirmware.inc', 'bsd/dev/sweet_ufs.c', 'bsd/conf/files',
         'bsd/dev/sweet_wpss.c', 'bsd/dev/wifi/WpssGate.h', 'bsd/dev/wifi/WpssRuntime.inc',
         'bsd/dev/modem/ModemGate.h',          'bsd/dev/modem/QcomSmem.c', 'bsd/dev/modem/QcomSmem.h', 'bsd/dev/modem/QcomSmp2p.c',
         'bsd/dev/modem/QcomSmp2p.h', 'bsd/dev/modem/QcomRpmh.c', 'bsd/dev/modem/QcomRpmh.h', 'bsd/dev/modem/QcomQmp.h',
         'bsd/dev/modem/QcomScm.h',
         # 2246: the WPSS edge links against the shared GLINK and QRTR, which 2246 does not change but the model compiles.
         # 2248 adds the lookup to that QRTR and the WLFW client on the edge.
         'bsd/dev/modem/QcomGlink.c', 'bsd/dev/modem/QcomGlink.h', 'bsd/dev/modem/QcomQrtr.c', 'bsd/dev/modem/QcomQrtr.h',
         # 2248: the WLFW client's own header, created by that patch.
         'bsd/dev/wifi/WlfwQmi.h',
         # 2253 (W9): the IPA driver's page-table builder, its allocator and its explicit-asm access forms. W9 includes all
         # three, so the scratch tree has to carry them; they are unchanged by 2253, and the byte-for-byte comparison then
         # proves the patch did not touch them.
         'bsd/dev/modem/IpaSmmu.h', 'bsd/dev/modem/IpaMmu.h', 'bsd/dev/modem/IpaMmio.h']
NEW = {'bsd/dev/sweet_wpss.c', 'bsd/dev/wifi/WpssGate.h', 'bsd/dev/wifi/WpssRuntime.inc', 'bsd/dev/wifi/WlfwQmi.h'}
RT = 'bsd/dev/wifi/WpssRuntime.inc'# (name, file, text to replace, replacement, text the failing run must print)
NEGATIVE = [
    ('no SMP2P stop before the TrustZone shutdown', RT,
     ' if(w.started && w.sp.out){\n', ' if(0){\n', 'stop->ack_before'),
    ('a prior WPSS item is not refused', RT,
     'if(qcom_smem_find(&w.sm,&w.part,W_IN,&prior,&bytes)!=QcomSmemMissing)',
     'if(qcom_smem_find(&w.sm,&w.part,W_IN,&prior,&bytes)!=QcomSmemMissing && 0)', 'W2 refused: WPSS SMP2P item present'),
    ('unmap of the offset address (ast188)', RT,
     'static void w_unmap(vm_offset_t va,vm_size_t bytes){vm_offset_t off=va&PAGE_MASK;ml_io_unmap(va-off,round_page(bytes+off));}',
     'static void w_unmap(vm_offset_t va,vm_size_t bytes){ml_io_unmap(va,bytes);}', 'UNALIGNED unmap'),
    ('an unverified stop releases memory', RT,
     'retaining memory, QMP state and power\\n");sweet_log_sync();return false;}',
     'retaining memory, QMP state and power\\n");sweet_log_sync();}', 'none_unmapped()'),
    ('a shared rail vote below the table maximum', RT,
     'value=i==2?3:w.rail_index[i];', 'value=i==2?3:w.rail_index[i]-1;', 'votes_never_lowered'),
    ('a modem or battery gate is not refused', 'bsd/dev/wifi/WpssGate.h',
     '    if(wpss_conflict(args))return WpssRefused;\n', '', 'WpssRefused'),
    ('the v5 SPMI driver instead of the v7 reader (W1 audit defect 1)', 'bsd/dev/sweet_wpss.c',
     '  wpss_gpio5_survey();\n',
     '  if(sweet_spmi_init()==0){uint8_t v=0;int rc=sweet_spmi_read(0,0xbc04,&v);printf("SWEET WIFI: W1 pmk8550 gpio5 reg=0xbc04 rc=%d value=%#x\\n",rc,v);}\n',
     'v5 SPMI driver'),
    ('the survey makes a PAS call', RT,
     ' printf("SWEET WIFI: W1 read-only survey begin; PAS6 untouched\\n");',
     ' printf("SWEET WIFI: W1 read-only survey begin; PAS6 untouched\\n");(void)w_pas(QCOM_SCM_PAS_SHUTDOWN,1,0,0);', '!pas_ncalls'),
    ('the pin step writes a fourth register', RT,
     ' if(rc)goto out;\n for(unsigned i=0;i<sizeof(regs);i++){uint8_t a=0;',
     ' if(!rc)rc=w_spmi_write(ch,0x45,0x01);\n if(rc)goto out;\n for(unsigned i=0;i<sizeof(regs);i++){uint8_t a=0;', 'pmic_nwrites==3'),
    ('a pin write is not read back', RT, ' || back!=to[i]){', '){', 'pmic_nwrites==2'),
    ('the pin APID owner is not checked', RT, '  if(o==0)apid=(int)n;\n', '  apid=(int)n;\n', 'needs exactly one APID'),
    ('the pin is written from an unexpected state', RT,
     ' if(!stock){printf("SWEET WIFI: W1c refused: mode/pull/source %#x/%#x/%#x are neither W1\'s state nor the target; no write\\n",v[3],v[5],v[7]);goto out;}\n',
     ' (void)stock;\n', '!pmic_nwrites && logged("refused")'),
    ('W2 starts the WPSS without the sleep-clock pin', RT, ' if((rc=wpss_gpio5_set()))goto out;\n', '', 'pmic_nwrites==3'),
    ('the shutdown is not owed before init_image', RT,
     ' w.owed=true;\n if((rc=w_pas(QCOM_SCM_PAS_INIT_IMAGE', ' if((rc=w_pas(QCOM_SCM_PAS_INIT_IMAGE', 'count_pas(6)==1'),
    # 2244, from ast208: the two SMP2P items were the wrong way round, so the ready bit could never arrive. Both directions
    # are reverted separately; each must make the start time out with ready=0.
    ('the WPSS and host SMP2P items are swapped for reading (ast208)', RT,
     '#define W_IN 617u', '#define W_IN 616u', 'final_error=60'),
    ('the WPSS and host SMP2P items are swapped for writing (ast208)', RT,
     '#define W_OUT 616u', '#define W_OUT 617u', 'final_error=60'),
    # The ready wait must be longer than the DSP's first-boot budget: with an 18 s cold start, ast208's 10 s cuts it off.
    ('the ready wait is cut back to ast208\'s 10 s', RT,
     '#define W_READY_MS 40000u', '#define W_READY_MS 10000u', 'final_error=60'),
    ('the state probe writes to the GIC', RT,
     ' if(pending&(1u<<bit))printf', ' w_wr(gic,0x280u+4u*word,1u<<bit);if(pending&(1u<<bit))printf', 'gic_writes==0'),
    # 2245: the watchdog's INTID is SPI + 32. Using the SPI number as an INTID reads a different interrupt and never reports
    # a bite, which is silent rather than loud, so it gets its own control.
    ('the watchdog INTID is taken to be the SPI number (ast218)', RT,
     '#define W_WDOG_INTID (W_WDOG_SPI + 32u)', '#define W_WDOG_INTID W_WDOG_SPI', 'intid=611 word=19 bit=3'),
    # 2246 (W3). The point of W3 is that the edge comes up and that a start still stops cleanly when it does not, so the
    # two controls are "the link is not opened at all" and "the link is not closed before the DSP is told to stop".
    ('W3 never opens the link after handover', RT,
     'if(w.link_asked && (rc=w_glink_open()))', 'if(0 && (rc=w_glink_open()))', 'W3 glink IPCRTR up=1'),
    ('W3 leaves the link open across the stop', RT,
     ' bool acked=false;\n w_wlfw_close();\n w_glink_close();\n', ' bool acked=false;\n', 'W3 glink close: up=1'),
    # The hello must not be sent before the channel can carry it: QcomQrtr counts a refused send and never retries.
    ('the QRTR hello is sent before the channel can carry it', RT,
     'if(w.glch->remote[k].valid && !w.glch->remote[k].busy){can_send=true;break;}',
     'if(1){can_send=true;break;}', 'refused=1'),
    # 2247 (W3 audit note 9): a link that is up with no intent from the WPSS must say so, or a boot where our HELLO never
    # left reads exactly like a silent firmware.
    ('a HELLO that never leaves is not logged', RT,
     ' else if(!w.hello_waiting && w_now()-w.ch_up_at>=W_HELLO_GRACE_MS){', ' else if(0){',
     'hello waiting: the link is up'),
    # 2248 (W4). The client's own safeguards, one control each.
    # The lookup is what makes us a client rather than a listener: a firmware that announces nothing is found only by
    # asking. This breaks the asking, and the test that expects the request (with the firmware announcing nothing) must
    # fail on the missing line.
    ('W4 can never ask the name service', 'bsd/dev/modem/QcomQrtr.c',
     '    if (q->remote_node == QCOM_QRTR_NODE_BROADCAST) return -1;',
     '    if (q->remote_node == QCOM_QRTR_NODE_BROADCAST || 1) return -1;',
     'W3 lookup service=0x45 instance=0x301 asked'),
    # A request whose reply never comes must not be waited on for ever, and the next one must not be sent regardless.
    # 2248b: the timeout branch now sits under `if(w.wlfw_sent)` and prints the wait it really had, so this control turns
    # the branch off. The unanswered-request test then never sees its line.
    ('W4 waits for a reply for ever', RT,
     ' if(w.wlfw_sent){\n  /* 2248b (defect 1): clear wlfw_sent with the step.',
     ' if(0){\n  /* 2248b (defect 1): clear wlfw_sent with the step.',
     'W4 wlfw cap no reply in 5000 ms; the rest is not sent'),
    # A refused request must stop the sequence: sending three more to a firmware that has just said no is how a probe
    # turns into something else.
    ('W4 sends the next request after a refusal', RT,
     '  w.wlfw_step=W4Done;w.wlfw_sent=false;return;\n }\n switch(m.message){',
     '  w.wlfw_sent=false;w.wlfw_step++;return;\n }\n switch(m.message){',
     'the rest is not sent'),
    # The register window is recorded and never touched. A mapping of it is the one thing the reference port's kernel died
    # of, and the model aborts on any mapping it did not expect, so this control proves the code never asks for one.
    ('W4 maps the register window', RT,
     '  int rc=wlfw_device_info(data,bytes,&addr,&size);',
     '  int rc=wlfw_device_info(data,bytes,&addr,&size);if(!rc)(void)ml_io_map_unmappable((vm_offset_t)addr,0x1000,VM_WIMG_IO);',
     'unexpected mapping'),
    # The client's port must be dropped before the link is: a reply routed to a port that is gone is the case the close
    # exists for, and w_glink_close now says so if it ever happens.
    ('W4 closes the link before its own port', RT,
     ' w_wlfw_close();\n w_glink_close();', ' w_glink_close();\n w_wlfw_close();',
     'the link closed before the WLFW client port'),
    # The gate must refuse two edge gates at once, or one boot could start two conversations on the same link. This drops
    # the count check's refusal, and the gate tests' `<=0` assertion on a refused spelling fails as a consequence.
    ('W4 is allowed beside the W3 gate', 'bsd/dev/wifi/WpssGate.h',
     'if((probe!=0)+(start!=0)+(pin!=0)+(glink!=0)+(wlfw!=0)+(bdf!=0)+(mode!=0)+(mission!=0)+(smmu!=0)+(dma!=0)>1)return WpssRefused;',
     'if(0)return WpssRefused;',
     'wpss_gate(off[i],deadman)<=0'),
    # 2248b (W4 audit defect 1). The guard in the receive path is what stops a late or repeated reply from walking the
    # step past W4Done. It has two halves, and each control removes the half its scenario needs.
    #
    # (a) the whole guard, which is what the old code had. The late-reply test then finds no `dropped reply` line,
    # because the reply was taken and the step advanced.
    ('W4 accepts a reply when no request is outstanding (late reply)', RT,
     'if(!w4_reply_expected(w.wlfw_sent,w.wlfw_step,w.wlfw_transaction,m.message,m.transaction)){',
     'if(0){',
     'W4 wlfw dropped reply id=0x34'),
    # (b) only the message-id half: a reply with the wrong id, at the right transaction, is accepted and advances the
    # step. The direct guard assertion fails first, then the wrong-id test's log line.
    ('W4 accepts a reply for a different message id', RT,
     'return message==w4_step_message(step) && reply_transaction==transaction;',
     'return reply_transaction==transaction;',
     '!w4_reply_expected(true,W4HostCap,2,WLFW_DEVICE_INFO_REQ,2)'),
    # (c) the clamp on the step-name lookup. The test calls w4_step_str with an out-of-range step directly, so this
    # control reproduces the audit's fault: a global-buffer-overflow read past w4_step_name in rodata.
    ('W4 indexes the step name without a clamp', RT,
     'static const char *w4_step_str(uint32_t step){return w4_step_name[step<W4_STEPS?step:W4Done];}',
     'static const char *w4_step_str(uint32_t step){return w4_step_name[step];}',
     'global-buffer-overflow'),
    # 2248b (defect 3): the dwell clamp. Without it the last reply bound runs to its full 5 s, past the dwell's end, which
    # is the audit's 23 s worst case. The 23 s test asserts the clamped wait, so this control fails on that line.
    ('W4 bounds a reply past the end of the dwell', RT,
     ' return want<w.wlfw_dwell_end?want:w.wlfw_dwell_end;   /* never later than the dwell allows */',
     ' return want;',
     'W4 wlfw device_info no reply in '),
    # 2249 (W5). The board data download's own safeguards, one control each.
    #
    # (a) The board file choice. ast234's CAP reply carried no board id, so ath11k's fallback is 0xff and the vendor's own
    # rule maps 0xff to bdwlan.bin. A control that makes an absent id look like id 0 would open bdwlan.b00, which does not
    # exist, and the stage's own line about the file would never print.
    ('W5 treats an absent board id as a real one', 'bsd/dev/wifi/WpssRuntime.inc',
     ' if(!w.wlfw_board_present || w.wlfw_board==0xffu){',
     ' if(!w.wlfw_board_present)w.wlfw_board=0;\n if(0){',
     'W5 bdf board: board=0xff'),
    # (b) The piece size must leave room for the QRTR header and the request's TLVs. The first draft left 48 and forgot the
    # header, so a 0x800 intent took a 2000-byte piece whose packet was 2032 bytes: QcomGlink refused it for ever, the file
    # never completed, and the dwell ran out. With the small-intent test asserting the count, the wrong overhead shows up
    # as a piece that is never sent and a run that ends at the dwell.
    ('W5 sizes a piece without leaving room for the header', RT,
     '#define W_BDF_OVERHEAD 80u', '#define W_BDF_OVERHEAD 48u',
     'W4 wlfw regdb not sent: '),
    # (c) A piece's reply is what advances the file: bdf_sent moves only on an accepted reply. A control that counts the
    # reply but does not advance bdf_sent re-sends piece 0 for ever, so the file never completes and the stage never
    # reaches init_done. That is the sequence's whole point: no piece goes out on top of an unanswered one.
    ('W5 counts a piece reply without advancing the file', RT,
     '  w.bdf_accepted++;w.bdf_sent+=w.bdf_piece_len;w.bdf_seg++;',
     '  w.bdf_accepted++;w.bdf_seg++;',
     'W5 bdf board: board=0xff board_present=1 variant=default bytes=58180 type=0'),
    # (d) The last piece alone carries end=1. Sending end=1 early tells the firmware the file is complete when most of it
    # is still unsent: the peer in the model refuses it exactly as the real firmware would, and the file never finishes.
    ('W5 marks every piece as the last', RT,
     'const uint8_t end=(w.bdf_sent+take>=w.bdf_bytes)?1:0;', 'const uint8_t end=1;',
     'W5 bdf regdb stopped at piece='),
    # (e) The board data stage must not run when the gate is W4: W4's own audit covers an image that stops after
    # DEVICE_INFO, and a W4 boot that started downloading files would be a different boot from the one it was approved for.
    # The control makes W4 ask for board data too; the W4 exchange test then sees more than four QMI requests.
    ('W5 downloads board data on the W4 gate too', RT,
     ' w.bdf_asked=(gate==WpssBdf || gate==WpssMode || gate==WpssMission || gate==WpssSmmu || gate==WpssDma);',
     ' w.bdf_asked=(gate==WpssBdf || gate==WpssMode || gate==WpssMission || gate==WpssSmmu || gate==WpssDma || gate==WpssWlfw);',
     'rgw.requests==4'),
    # (f) The dwell. On a healthy link the download is fast (about 2 s for 41 pieces), so W4's 20 s would usually hold it;
    # what it cannot hold is a firmware that is slow to answer plus the 10 s init_done wait. A control that makes every
    # piece slow shows the longer dwell is what keeps the board file inside the run rather than being cut off by the stop.
    ('W5 uses the W4 dwell, too short for a slow firmware', RT,
     ' w.dwell_ms=w.mission_asked?W_MISSION_DWELL_MS:(w.mode_asked?W_MODE_DWELL_MS:(w.bdf_asked?W_BDF_DWELL_MS:W_DWELL_MS));',
     ' w.dwell_ms=w.mission_asked?W_MISSION_DWELL_MS:(w.mode_asked?W_MODE_DWELL_MS:W_DWELL_MS);',
     'W5 bdf board complete: pieces=10'),
    # (g) The file_id every piece carries is the board id CAP reported (or ath11k's 0xff fallback), and the firmware uses
    # it to know which board file this is. A control that sends the raw, absent board id 0 instead means the peer's own
    # name-for-id rule has no file, so the first piece is refused: exactly what the real firmware would do with a board
    # data file that is not the one its id names.
    ('W5 sends file_id 0 instead of the fallback', RT,
     '    w.wlfw_board_present?w.wlfw_board:0xffu,w.bdf_bytes-w.bdf_sent,w.bdf_seg,w.bdf_piece,take,end,(uint8_t)w.bdf_type);',
     '    w.wlfw_board,w.bdf_bytes-w.bdf_sent,w.bdf_seg,w.bdf_piece,take,end,(uint8_t)w.bdf_type);',
     'W5 bdf board: board='),
    # (h) init_done is only taken once every piece has been answered, and the stage's own guard enforces that: the
    # indication is ignored unless the step is the init_done wait and bdf_sent has reached bdf_bytes. A control that drops
    # the two conditions takes an indication at any time, which on the slow-firmware case means the stage reports success
    # while a file is still half sent. The slow case is the one that reaches it, because that is where an indication can
    # arrive early at all.
    ('W5 accepts init_done before the file is finished', RT,
     'if(m.message==WLFW_FW_INIT_DONE_IND && w.wlfw_step==W4InitDone && w.bdf_sent>=w.bdf_bytes && !w.bdf_init_done){',
     'if(m.message==WLFW_FW_INIT_DONE_IND && !w.bdf_init_done){',
     'text_order("W5 bdf board complete","W5 bdf init_done=1"'),
    # 2250 (W6). The mode stage's own safeguards, one control each.
    #
    # (a) The firmware's memory. On this chip it is the fixed wlan_msa_mem carve-out and ath11k asks QMI for nothing, so
    # this stage allocates nothing and answers no request_mem. A control that makes the host answer one would be the
    # beginning of a carve-out nobody audited: the model's request_mem line says "not allocated, not answered", and the
    # control's failure is that line changing.
    ('W6 answers a request_mem it never registered for', RT,
     'printf("SWEET WIFI: W6 mode request_mem segments=%u first=%s bytes=%u configs=%u; not allocated, not answered\\n",',
     'printf("SWEET WIFI: W6 mode request_mem segments=%u first=%s bytes=%u configs=%u; allocated and answered\\n",',
     'not allocated, not answered'),
    # (b) The order. W6's answer is the mode request AFTER init_done, because that is where ath11k sends it; a stage that
    # asked for the mode before the board data was accepted would be asking a firmware that is not initialised yet. This
    # control makes the mode step run without waiting for init_done, and the W5 regression test then sees a W6 line in a
    # W5 boot as well as the stage's own order assertion failing.
    ('W6 asks for the mode before the board data is accepted', RT,
     '  if(!w.mode_asked)return;\n  w.wlfw_step=W4Mode;',
     '  if(!w.mode_asked)return;\n  if(1){w.wlfw_step=W4Mode;w.wlfw_deadline=now+W_W4_GAP_MS;return;}\n  w.wlfw_step=W4Mode;',
     'W6 mode next:'),
    # (c) The mode number. Mode 7 is cold-boot calibration and 0 is mission mode; sending 0 here would put the radio in
    # mission mode in a stage whose audit says it does not, which is the one thing L3 is for.
    ('W6 asks for mission mode instead of calibration', RT,
     'WLFW_MODE_COLD_BOOT);break;', 'WLFW_MODE_NORMAL);break;',
     'W6 mode next: cold-boot calibration (mode 7)'),
    # (d) Both of the stage's answers must be taken, in whichever order the firmware sends them. A host that only accepts
    # cold_boot_cal_done while it is at the cal_done step loses it when fw_ready arrives first (which a real firmware may
    # do, and this stage exists to notice), and then reports a stage that never calibrated. The reverse-order test is what
    # fails.
    ('W6 loses cal_done when fw_ready comes first', RT,
     'if(m.message==WLFW_COLD_BOOT_CAL_DONE_IND && mode_waiting && !w.mode_cal_done){',
     'if(m.message==WLFW_COLD_BOOT_CAL_DONE_IND && w.wlfw_step==W4CalDone && !w.mode_cal_done){',
     'W6 mode cold_boot_cal_done=1 after '),
    # (e) The dwell. 20 s cannot hold 41 pieces, a 10 s init_done wait, a mode request and a 20 s calibration wait, so a
    # gate that used W4's dwell would stop before its own answer. The slow-firmware W5 case asserts the count that only
    # the longer dwell reaches.
    ('W6 uses the W4 dwell, too short for the board data', RT,
     ' w.dwell_ms=w.mission_asked?W_MISSION_DWELL_MS:(w.mode_asked?W_MODE_DWELL_MS:(w.bdf_asked?W_BDF_DWELL_MS:W_DWELL_MS));',
     ' w.dwell_ms=w.mission_asked?W_MISSION_DWELL_MS:(w.mode_asked?W_DWELL_MS:(w.bdf_asked?W_BDF_DWELL_MS:W_DWELL_MS));',
     'W6 mode next:'),
    # (f) The stage must not send WLAN_CFG or mission mode. Both belong to ath11k_qmi_firmware_start at mission mode, and
    # a stage that sent the CE configuration would describe rings that were never built. The control adds a config request
    # to the mode step; the close line's transaction count is what changes, and the test's own byte-exact mode assertion
    # is what fails first.
    ('W6 sends the WLAN configuration', RT,
     '  case W4Mode: bytes=wlfw_wlan_mode(w.wlfw_buf,sizeof(w.wlfw_buf),(uint16_t)(++w.wlfw_transaction),WLFW_MODE_COLD_BOOT);break;',
     '  case W4Mode: bytes=wlfw_wlan_mode(w.wlfw_buf,sizeof(w.wlfw_buf),(uint16_t)(++w.wlfw_transaction),WLFW_MODE_COLD_BOOT);'
     'printf("SWEET WIFI: W4 wlfw wlan_cfg request bytes=8\\n");break;',
     'W4 wlfw close: step=done tx=19'),
    # 2251 (W7). The mission half's own safeguards, one control each.
    #
    # (a) The firmware log. ath11k_qmi_wlanfw_wlan_ini_send sends enablefwlog = 1 when fw_wmi_diag_event is set, which
    # core.c sets for this chip; a stage that sent 0 would be asking for the log to stay off. The peer reads the value out
    # of the request and refuses anything but 1, exactly as a firmware that needs the log on would, so the run stops at
    # wlan_ini instead of going on to the configuration.
    ('W7 asks for the firmware log to stay off', RT,
     ' case W4Ini: bytes=wlfw_wlan_ini(w.wlfw_buf,sizeof(w.wlfw_buf),(uint16_t)(++w.wlfw_transaction),1);break;',
     ' case W4Ini: bytes=wlfw_wlan_ini(w.wlfw_buf,sizeof(w.wlfw_buf),(uint16_t)(++w.wlfw_transaction),0);break;',
     'W4 wlfw refused at wlan_ini'),
    # (b) The mode number. Mission mode is 0. A stage that asked for cold boot again, or for off, would never leave the
    # calibration state, and the test's own assertion on the peer's mission count is what fails.
    ('W7 asks for cold boot again instead of mission mode', RT,
     '   bytes=wlfw_wlan_mode(w.wlfw_buf,sizeof(w.wlfw_buf),(uint16_t)(++w.wlfw_transaction),WLFW_MODE_NORMAL);break;',
     '   bytes=wlfw_wlan_mode(w.wlfw_buf,sizeof(w.wlfw_buf),(uint16_t)(++w.wlfw_transaction),WLFW_MODE_COLD_BOOT);break;',
     'W7 mission taken'),
    # (c) The shadow list. ath11k sends the v2 list; a host that sent the older 0x13 form, or none at all, is describing a
    # different set of rings. The peer decodes 0x13 as an error, so the configuration is refused and the run stops there.
    ('W7 sends the older shadow register list', 'bsd/dev/wifi/WlfwQmi.h',
     '    out[at] = WLFW_TLV_SHADOW_V2;',
     '    out[at] = WLFW_TLV_SHADOW;',
     'W4 wlfw refused at wlan_cfg'),
    # (d) The shadow addresses' direction offset. Only the destination-status ring takes ath11k's +4; a list that added it
    # to every destination ring's TP would point the firmware four bytes past the word it must shadow.
    ('W7 shadows the wrong word of every destination ring', 'bsd/dev/wifi/WlfwQmi.h',
     '            out[n++] = WLFW_CE_DST_BASE + WLFW_CE_STRIDE * i + WLFW_CE_HP;\n',
     '            out[n++] = WLFW_CE_DST_BASE + WLFW_CE_STRIDE * i + WLFW_CE_HP + WLFW_HP_TO_TP;\n',
     'cfg_version_ok && cfg_pipes_ok && cfg_svcs_ok && cfg_shadow_ok'),
    # (e) The pipe directions. PIPEDIR_IN is 1 and OUT is 2; swapping them would tell the firmware to receive on the pipes
    # the host transmits on. The peer compares every field with its own copy of the table.
    ('W7 inverts the copy engine directions', 'bsd/dev/wifi/WlfwQmi.h',
     '    {0, 2, 32, 2048, 0},    /* CE0 host->target HTC control and raw streams */',
     '    {0, 1, 32, 2048, 0},    /* CE0 host->target HTC control and raw streams */',
     'W4 wlfw refused at wlan_cfg'),
    # (f) The mode-off. It is sent only after mission mode was taken, because the reference port's firmware refused it
    # with error 48 before that. The way to get it wrong is to route a failure to the mode-off, which is what a "clean up
    # with an explicit mode" instinct would do; the control sends it after a refused configuration instead of ending. The
    # peer refuses it (mission mode was never taken) and the test's own assertion that no mode-off was sent fails.
    ('W7 sends the mode-off after a refused configuration', RT,
     '  w.wlfw_step=W4Done;w.wlfw_sent=false;return;\n }\n switch(m.message){',
     '  w.wlfw_step=(w.mission_asked && w.wlfw_step>=W4Cfg)?W4ModeOff:W4Done;w.wlfw_sent=false;return;\n }\n switch(m.message){',
     'W7 mission close: ini=1 cfg=0 mission_sent=0 mission=0 mode_off_sent=0 mode_off=-1/-1'),
    # (g) The close line's own evidence. A stage that counted the mode-off as sent when it never left would make a failed
    # boot read as a clean one; the counter is set where the send succeeds, not where the step is entered.
    ('W7 counts a mode-off that never went out', RT,
     '  if(w.wlfw_step==W4ModeOff)w.mode_off_sent=true;\n',
     '',
     'mode_off_sent=1'),
    # (h) The fixed observation window. Without it the stage would leave mission mode the instant it arrived, which is not
    # the thing it exists to observe; the test asserts the hold's own line.
    ('W7 leaves mission mode with no observation', RT,
     '  w.wlfw_step=W4Observe;w.hold_until=w_now()+W_MISSION_HOLD_MS;',
     '  w.wlfw_step=W4ModeOff;w.wlfw_deadline=w_now()+W_W4_GAP_MS;',
     'W7 mission held 5000 ms'),
    # (i) The reply guard's step bound. Lowering it to the mission request would make the mode-off's own reply unroutable,
    # which is W4's audit defect 1 at the other end of the sequence.
    ('W7 admits no reply after the mission request', RT,
     ' if(!sent || step<W4IndRegister || step>W4ModeOff)return false;',
     ' if(!sent || step<W4IndRegister || step>W4Mission)return false;',
     'W7 mode off answered result=0 error=0'),
    # (j) The gate. W7 must be exclusive with W6, or one boot could send the mission half on top of a stage that was
    # audited to stop at fw_ready.
    # 2251: W7's exclusion from W6 is enforced three times over, and each alone is enough: the mode branch's own edge call
    # refuses the pair, the mission branch's refuses it, and the token count refuses it. No single edit can break the pair,
    # which is the rule working as intended, so this control tests the part of W7's gate that ONE edit does break: the
    # mission branch's call into the shared edge rule. Without it a W7 line carries a preset token or a second Wi-Fi token
    # without being refused, and the test's own assertions on those lines fail.
    ('W7 gate skips the shared edge rules', 'bsd/dev/wifi/WpssGate.h',
     '    if(mission){\n        if(wpss_edge_gate(args,deadman,"sweetwmission"))return WpssRefused;\n        return WpssMission;\n    }',
     '    if(mission){\n        return WpssMission;\n    }',
     'wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmission=0",600)==WpssRefused'),
    # 2253 (W9). This is the first stage that writes an SMMU register, so its controls are about what makes a write safe:
    # the bank is disabled before it is configured, the table is synced, the publish order is SMR last, the lease is not
    # adopted from someone else's words, the teardown routes to fault first and restores what it found, the bank is handed
    # back free by the predicate, and the two rules that keep IPA out of this bank and this stage out of IPA's.
    #
    # (a) The bank is enabled before it is configured: the model's phase machine refuses the enable, so the install stops
    # with nothing published and the run ends cleanly rather than booting a bank with no table.
    ('W9 enables the bank before its table is in place', RT,
     ' w_smmu_wr(s,b+0x10,IPA_MMU_TCR2);w_smmu_wr(s,b+0x30,IPA_MMU_TCR);',
     ' w_smmu_wr(s,b,w.dma_sctl);w_smmu_wr(s,b+0x10,IPA_MMU_TCR2);w_smmu_wr(s,b+0x30,IPA_MMU_TCR);',
     'SMMU write outside the W9 lease'),
    # (b) The publish order reversed: the SMR before the bank is enabled. A stream would reach a bank that is still off,
    # which on the real SMMU is a fault on the device's first DMA. The model refuses it.
    ('W9 publishes the SMR before the bank is enabled', RT,
     ' w_smmu_wr(s,b+0x618,0);       /* the invalidate word ipa_mmu_install writes for a leased VMID */\n w_smmu_wr(s,b,w.dma_sctl);',
     ' w_smmu_wr(s,0x800+4*w.dma_smr,w.dma_smr_raw);\n w_smmu_wr(s,b+0x618,0);\n w_smmu_wr(s,b,w.dma_sctl);',
     'SMMU write outside the W9 lease'),
    # (c) The SMR mask dropped: id 0x1400 with mask 0 covers 0x1400 only, so 0x1401 would fault. The model requires mask 1.
    ('W9 claims 0x1400 without the mask that covers 0x1401', RT,
     ' w.dma_smr_raw=0x80000000u|(1u<<16)|W9_SID;',
     ' w.dma_smr_raw=0x80000000u|W9_SID;',
     'SMMU write outside the W9 lease'),
    # (d) The wrong stream id: a plausible slip (IPA's own SID), which would take a stream IPA owns.
    ('W9 routes IPA\'s stream instead of the device\'s', RT,
     ' w.dma_smr_raw=0x80000000u|(1u<<16)|W9_SID;',
     ' w.dma_smr_raw=0x80000000u|(1u<<16)|0x4a0u;',
     'SMMU write outside the W9 lease'),
    # (e) The lease marker left off CBAR. This is the rule that keeps IPA's allocator out of this bank: without it the bank
    # reads back as free and IPA's next run could take it while this stage still routes to it.
    ('W9 leaves the CBAR lease marker off its bank', RT,
     ' w_smmu_wr(s,0x1000+4*w.dma_bank,W9_CBAR|w.dma_vmid);',
     ' w_smmu_wr(s,0x1000+4*w.dma_bank,w.dma_vmid);',
     'SMMU write outside the W9 lease'),
    # (f) The bank taken without checking the predicate: bank 0, which ABL's own SMRs route to. This stage has two guards
    # against it and the second is the one that fires, which is the point: the free-bank predicate is not the only thing
    # standing between this stage and someone else's bank. The lease's own recheck, taken right before the first write,
    # sees CBAR and CBA2R already set and refuses, so nothing is written and the run stops before any WPSS work.
    ('W9 takes a bank another stream already routes to', RT,
     '  for(unsigned c=0;c<W_SMMU_NCB && w.dma_bank==W9_NONE;c++)if(w9_bank_free(c,used))w.dma_bank=c;',
     '  w.dma_bank=0;',
     'slot 5 or bank 0 changed after the choice'),
    # (g) The teardown without the route-to-fault. Both the write and its own read-back go, so the code carries on as an
    # unsafe implementation would, and the model is what catches it: the release's first write is then the bank disable,
    # with the stream still routed to it. That is exactly the hole this control exists for.
    ('W9 tears the table down while a stream still routes to it', RT,
     ' w_smmu_wr(w.dma_smmu,0xc00+4*s_,2u<<16);w.dma_fault_routed=true;\n if(w_smmu_rd(w.dma_smmu,0xc00+4*s_)!=(2u<<16)){printf("SWEET WIFI: W9 dma: S2CR will not route to fault; retained\\n");sweet_log_sync();w.dma_verify_fail++;ok=false;goto out;}',
     ' w.dma_fault_routed=true;',
     'SMMU write outside the W9 lease phase 3'),
    # (h) The teardown that leaves CBAR set: the bank stays leased by the predicate, so it is never handed back.
    ('W9 leaves its bank leased after the teardown', RT,
     ' w_smmu_wr(w.dma_smmu,0x1000+4*w.dma_bank,w.dma_cbar_old);w_smmu_wr(w.dma_smmu,0x1800+4*w.dma_bank,w.dma_cba2r_old);',
     ' w_smmu_wr(w.dma_smmu,0x1800+4*w.dma_bank,w.dma_cba2r_old);',
     'W9 dma: released'),
    # (i) An install that proceeds on a stream somebody already matches: a match conflict on the first DMA.
    ('W9 installs over a stream another SMR already matches', RT,
     '    taken[k]=true;printf("SWEET WIFI: W9 dma: sid %#x is already matched by SMR %u raw=%#x; not installed\\n",W_WLAN_SID+k,i,smr);\n    sweet_log_sync();goto out_unmap;',
     '    taken[k]=true;',
     'W9 dma: sid 0x1400 is already matched'),
    # (j) The IOVA window above 4 GiB. ath11k sets a 32-bit host DMA mask on this bus while the SMMU itself would take
    # addresses up to 2^36, so that bound is the host's to keep. The control moves the ring's window one 4 GiB step up:
    # legal for the SMMU, refused by this stage before it maps or writes anything.
    ('W9 lets its IOVA window run past 4 GiB', RT,
     '#define W9_RING_IOVA W9_IOVA',
     '#define W9_RING_IOVA (W9_IOVA+0x100000000ull)',
     'W9 dma: the IOVA window '),
    # (j2) The doorbell mapped with the ring's attribute. A Device register page walked through a Normal-NC mapping is the
    # kind of mistake that only shows up as a hang on the first MSI, so the model checks the attribute at the walk.
    ('W9 maps the doorbell with the ring\'s Normal attribute', RT,
     '    ipa_pt_map_attr(&pt,W9_DOORBELL_IOVA,W9_DOORBELL_PA,W9_DOORBELL_BYTES,1)){',
     '    ipa_pt_map(&pt,W9_DOORBELL_IOVA,W9_DOORBELL_PA,W9_DOORBELL_BYTES)){',
     'W9 dma: the table does not resolve as asked'),
    # (k) The claim not taken at teardown: an SMMU write while another user may be mid-update.
    ('W9 releases without the apps_smmu claim', RT,
     ' if(!w9_claim()){printf("SWEET WIFI: W9 dma: busy at teardown, the claim was not taken; retained until reset\\n");sweet_log_sync();return false;}',
     '',
     'SMMU read without the claim'),
    # (l) The gate's call into the shared edge rule, the part of W9's gate one edit can break (see W7's and W8's controls).
    ('W9 gate skips the shared edge rules', 'bsd/dev/wifi/WpssGate.h',
     '    if(dma){\n        if(wpss_edge_gate(args,deadman,"sweetwdma"))return WpssRefused;\n        return WpssDma;\n    }',
     '    if(dma){\n        return WpssDma;\n    }',
     'wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=0",600)==WpssRefused'),
    # 2252 (W8 survey). The survey's whole claim is "read only, and only what this phone has already answered". Each control
    # breaks one half of that claim or one of the survey's own rules, and the fake SMMU or a test assertion catches it.
    # (a) A write, here the instinctive one: clearing the global fault status after logging it. From 2253 (W9) the fake no
    # longer aborts on every write, because W9 is allowed to write SMMU registers; instead every write is judged against the
    # lease model, and this one is outside every phase, so it fails as a write outside the W9 lease. The claim being tested is
    # unchanged: a survey that writes nothing cannot produce this line.
    ('W8 clears the global fault status it read', RT,
     ' w.smmu_taken|=1u<<which;w.smmu_gfsr[which]=gfsr;',
     ' w.smmu_taken|=1u<<which;w.smmu_gfsr[which]=gfsr;w_wr(s,0x48,gfsr);',
     'SMMU write outside the W9 lease'),
    # (b) Reading the stream-match table past the 114 entries ID0 reports. The hidden entries are the hypervisor's.
    ('W8 reads SMRs past the measured count', RT,
     '  uint32_t smr=w_smmu_rd(s,0x800+4*i);\n  if(!(smr>>31)){if(nfree<4)',
     '  uint32_t smr=w_smmu_rd(s,0x800+4*i);if(i+1==W_SMMU_NSMR)(void)w_smmu_rd(s,0x800+4*(i+1));\n  if(!(smr>>31)){if(nfree<4)',
     'SMMU read outside the proven set'),
    # (c) Reading the global fault syndrome, which no code of ours has ever read on this phone.
    ('W8 reads the global fault syndrome', RT,
     ' w.smmu_taken|=1u<<which;w.smmu_gfsr[which]=gfsr;',
     ' w.smmu_taken|=1u<<which;w.smmu_gfsr[which]=gfsr;(void)w_smmu_rd(s,0x54);',
     'SMMU read outside the proven set'),
    # (d) Reading the tables on a geometry nobody measured. 2253 (W9) added a second gate with the same shape (its own
    # install refuses an unmeasured geometry too), so the control names the survey's whole branch, printf and all, to stay
    # unique and to keep testing the survey's gate rather than the install's.
    ('W8 reads the tables on an unmeasured geometry', RT,
     ' if(id0!=W_SMMU_ID0 || id1!=W_SMMU_ID1 || id2!=W_SMMU_ID2){\n  printf("SWEET WIFI: W8 smmu %s: geometry differs',
     ' if(0){\n  printf("SWEET WIFI: W8 smmu %s: geometry differs',
     'geometry differs'),
    # (e) Reading without the apps_smmu claim, which is what keeps the survey from reading IPA's or touch's half-written entry.
    # 2253 (W9) split the claim into w9_claim for its own paths, so this names the survey's own loop, which is still the one
    # under test.
    ('W8 reads without the apps_smmu claim', RT,
     ' while(!sweet_apps_smmu_claim()){\n  if(waited>=W_SMMU_CLAIM_MS){printf("SWEET WIFI: W8 smmu %s: busy',
     ' while(0 && !sweet_apps_smmu_claim()){\n  if(waited>=W_SMMU_CLAIM_MS){printf("SWEET WIFI: W8 smmu %s: busy',
     'SMMU read without the claim'),
    # (f) The window left mapped. 2253 (W9) has its own unmap in the same wording (both close their own window the same way),
    # so this names the survey's line, which is preceded by its own tally store.
    ('W8 leaves the SMMU window mapped', RT,
     ' w.smmu_wlan=hits[0]+hits[1];w.smmu_valid=valid;w.smmu_free_smr=free_smr;w.smmu_free_cb=free_cb;w.smmu_faulted=faulted;\nout:\n w_unmap(s,W_SMMU_BYTES);sweet_apps_smmu_release();sweet_log_sync();',
     ' w.smmu_wlan=hits[0]+hits[1];w.smmu_valid=valid;w.smmu_free_smr=free_smr;w.smmu_free_cb=free_cb;w.smmu_faulted=faulted;\nout:\n sweet_apps_smmu_release();sweet_log_sync();',
     'all_unmapped()'),
    # (g) The SMR mask ignored: an entry for 0x1400 with mask 1 also covers 0x1401, and a survey that compared ids exactly
    # would call 0x1401 unmatched and send W8's ring stage after a second SMR that would conflict on the first DMA.
    ('W8 matches stream ids without the SMR mask', RT,
     'static bool w_smr_hits(uint32_t smr,unsigned sid){uint32_t mask=(smr>>16)&0x7fffu;return (smr>>31) && !((smr^sid)&0x7fffu&~mask);}',
     'static bool w_smr_hits(uint32_t smr,unsigned sid){return (smr>>31) && !((smr^sid)&0x7fffu);}',
     'sid 0x1401 matched by 1 smr'),
    # (h) No snapshot before the start: the later two then have no canary and no baseline to compare against. 2253 (W9)
    # inserts its own install between the two lines this used to name, so the control removes the survey call and the blank
    # line after it, which is unique to the survey and is what the W8 tests then fail on.
    ('W8 takes no snapshot before the start', RT,
     ' w_smmu_survey(0);\n /* 2253 (W9): the WLAN device',
     ' /* 2253 (W9): the WLAN device',
     'W8 smmu before: sCR0'),
    # (i) The FSR format field read as a fault: banks 0 to 4 read 0x400 on every boot, and calling that a fault would send
    # the next stage hunting for one.
    ('W8 counts the FSR format field as a fault', RT,
     '  if(fsr&~0x600u){faulted++;',
     '  if(fsr){faulted++;',
     'faulted=0'),
    # (j) The gate's call into the shared edge rule, the part of W8's gate one edit can break (see W7's control above).
    ('W8 gate skips the shared edge rules', 'bsd/dev/wifi/WpssGate.h',
     '    if(smmu){\n        if(wpss_edge_gate(args,deadman,"sweetwsmmu"))return WpssRefused;\n        return WpssSmmu;\n    }',
     '    if(smmu){\n        return WpssSmmu;\n    }',
     'wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwsmmu=1 sweetmpss=1",600)==WpssRefused'),
]


def run(cmd, **kw):
    return sp.run(cmd, capture_output=True, text=True, **kw)


def build(src, out, image):
    d = src / 'bsd/dev'
    cc = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-Wno-unused-function',
          '-Wno-unused-parameter', '-fsanitize=address,undefined', '-DWPSS_HOST_TEST', '-DADSP_HOST_TEST',
          # 2253 (W9): IPA_HOST_TEST makes IpaMmio.h's ipa_mmio_read32/write32 the host model's m_rd/m_wr, so every SMMU
          # access the Wi-Fi code makes, survey reads and W9 lease writes alike, lands on the model's checks instead of
          # bypassing them through the real inline asm.
          '-DIPA_HOST_TEST', '-DMODEM_PROTOCOL_HOST_TEST', '-I' + str(HERE / 'tests'), '-I' + str(d), '-I' + str(d / 'wifi'),
          str(HERE / 'tests/wpss.c'), str(d / 'modem/QcomSmem.c'), str(d / 'modem/QcomSmp2p.c'), str(d / 'modem/QcomRpmh.c'),
          str(d / 'modem/QcomGlink.c'), str(d / 'modem/QcomQrtr.c'),
          '-o', str(out)]
    r = run(cc)
    if r.returncode:
        return 'compile failed\n' + r.stderr
    r = run([str(out), image])
    return None if r.returncode == 0 else r.stdout + r.stderr


def main():
    tree, image = Path(sys.argv[1]).resolve(), sys.argv[2]
    os.environ['TMPDIR'] = '/tmp'
    # A fresh scratch directory each run, left in /tmp: this tool deletes nothing.
    if True:
        tmp = Path(tempfile.mkdtemp(prefix='wifi-check-', dir='/tmp'))
        src = tmp / 'src'
        for f in FILES:
            if f in NEW:
                continue
            blob = run(['git', 'show', 'wifi-base:' + f], cwd=tree)
            assert blob.returncode == 0, blob.stderr
            (src / f).parent.mkdir(parents=True, exist_ok=True)
            (src / f).write_text(blob.stdout)
        run(['git', 'init', '-q'], cwd=src)
        for p in PATCHES:
            r = run(['git', 'apply', '--whitespace=nowarn'] + ['--include=' + f for f in FILES] + [str(p)], cwd=src)
            if r.returncode:
                sys.exit('FAIL: %s does not apply: %s' % (p.name, r.stderr))
        for f in FILES:
            if (src / f).read_bytes() != (tree / f).read_bytes():
                sys.exit('FAIL: patches do not reproduce ' + f)
        print('PASS: %s reproduce the tree files byte for byte' % ', '.join(p.name[:4] for p in PATCHES))
        err = build(src, tmp / 'wpss', image)
        if err:
            print(err)
            sys.exit('FAIL: host model on the patched source')
        print(run([str(tmp / 'wpss'), image]).stdout, end='')
        # 2248: the WLFW encoder and decoder on their own, against bytes worked out by hand from the documented layout.
        r = run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                 '-I' + str(src / 'bsd/dev/wifi'), str(HERE / 'tests/wlfw.c'), '-o', str(tmp / 'wlfw')])
        if r.returncode:
            print(r.stderr)
            sys.exit('FAIL: the WLFW encoder tests do not compile')
        r = run([str(tmp / 'wlfw')])
        print(r.stdout, end='')
        if r.returncode:
            sys.exit('FAIL: the WLFW encoder tests')
        for i, (name, f, old, new, expect) in enumerate(NEGATIVE):
            neg = tmp / ('neg%d' % i)
            shutil.copytree(src, neg, ignore=shutil.ignore_patterns('.git'))
            text = (neg / f).read_text()
            assert text.count(old) == 1, 'negative control text not found once: ' + name
            (neg / f).write_text(text.replace(old, new))
            err = build(neg, tmp / ('wpss-neg%d' % i), image)
            if not err:
                sys.exit('FAIL: negative control passed: ' + name)
            if expect not in err:
                print(err)
                sys.exit('FAIL: negative control failed for another reason: ' + name)
            print('PASS: negative control fails as expected: ' + name)
    print('PASS: all Wi-Fi stage 1 host checks and %d negative controls' % len(NEGATIVE))


if __name__ == '__main__':
    main()
