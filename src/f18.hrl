-ifndef(__F18_HRL__).
-define(__F18_HRL__, true).

-ifndef(DEFAULT_VSN).
-define(DEFAULT_VSN, v221113).
-endif.

-define(USB_A, "/dev/serial/by-id/usb-GreenArrays_EVB002_Port_A_*").
-define(USB_B, "/dev/serial/by-id/usb-GreenArrays_EVB002_Port_B_*").
-define(USB_C, "/dev/serial/by-id/usb-GreenArrays_EVB002_Port_C_*").

-define(DEFAULT_BAUD, 460800).

-define(UDELAY(B), (trunc((1/(B))/(2.4E-9)))).

-endif.
