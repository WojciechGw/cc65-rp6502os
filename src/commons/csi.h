#define CSI_ESC "\x1b"
#define CSI CSI_ESC "["
#define CSI_RESET       CSI_ESC "c"
#define CSI_CLS         CSI "2J"
#define CSI_CURSOR_SHOW CSI "?25h"
#define CSI_CURSOR_HIDE CSI "?25l"
#define CSI_CURSOR_SCP  CSI "c"
#define CSI_CURSOR_RCP  CSI "u"
#define CSI_CURSOR_HOME CSI "1;1H"
#define CSI_ECHO_OFF    CSI "12h"   /* SRM: terminal stops echoing TX back to RX */
#define CSI_ECHO_ON     CSI "12l"   /* SRM: restore echo */

#define OSC CSI_ESC "]"
#define OSC_DEFAULT_COLORFG OSC "10;#"
#define OSC_DEFAULT_COLORBG OSC "11;#"
#define OSC_CURSOR_COLOR OSC "12;#"
#define OSC_ST CSI_ESC "\\"
#define OSC_RESET_COLORFG OSC "110" OSC_ST
#define OSC_RESET_COLORBG OSC "111" OSC_ST
#define OSC_RESET_CURSOR_COLOR OSC "112" OSC_ST