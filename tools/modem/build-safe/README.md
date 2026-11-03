# Local build cleanup

The cleanup wrapper is scoped to this machine's dedicated xnu-modem/dxb tree. It preserves final symlinks and invokes /usr/bin/trash on absolute paths only.

The active local MakeInc.cmd sets RM to plans/modem/build-safe/bin/cleanup -f and RMDIR to plans/modem/build-safe/bin/rmdir. That bin directory contains rm and rmdir symlinks to cleanup and must precede system directories in PATH. MakeInc.cmd also adds .PRECIOUS: % and .SECONDARY: so make retains failed/intermediate targets. Do not inject these via MAKEFILES.

Apply kextsymboltool-retain.patch to the dedicated tree before building. It prevents native error-path unlink. After any failed build, Trash its reported incomplete output before retrying.

Use the existing tree and the mandated --remake -j12 26.5 path only. No fresh build or clean command is covered. External compiler internals have not been exhaustively audited.
