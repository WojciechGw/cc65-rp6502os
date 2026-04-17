.export _initmainargs, initmainargs

.segment "STARTUP"
_initmainargs:
    rts

; Some runtimes may reference the unprefixed name.
initmainargs = _initmainargs
