SECTIONS {

.noinit (NOLOAD) : {
    . = ALIGN(4);
    __noinit_start = .;
    KEEP(*(.noinit*))
    __noinit_end = .;
} > RAM

.nobss : {
    . = ALIGN(4);
    __nobss_start = .;
    KEEP(*(.nobss*))
    __nobss_end = .;
} > RAM

}

SECTIONS {
.event_cb  : {
    . = ALIGN(4);
    __event_cb_start = .;
    KEEP(*(.event_cb*))
    __event_cb_end = .;
} > FLASH :text
.ui_uiew : {
    . = ALIGN(4);
    __ui_views_start = .;
    KEEP(*(.ui_view*))
    __ui_views_end = .;
} > FLASH :text

}
