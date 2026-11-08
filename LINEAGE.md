# LineageOS on asteroids

Unofficial LineageOS 23.2 for Nothing Phone (3a / 3a Pro). Not on https://wiki.lineageos.org/devices/.

Thread with current builds: https://xdaforums.com/t/rom-unofficial-lineageos-23-2-nothing-3a-3a-pro-asteroids.4783026/

Older builds: https://xdaforums.com/t/rom-unofficial-asteroids-lineageos.4764784/

Download only from that maintainer. The zip names they used are `lineage_asteroids-img.zip` (fastboot) and `lineage_asteroids-ota.zip` (sideload).

From the maintainer:

```
fastboot erase metadata && fastboot erase userdata && fastboot update lineage_asteroids-img.zip
```

or recovery sideload of the OTA zip after flashing their `vendor_boot.img`.

That wipes userdata, which is where this phone's iOS root lives. Keep `backup-asteroids/` on the Mac if you want iOS later.
