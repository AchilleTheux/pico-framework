# can_frame

The CAN message types shared by every controller backend in this framework:
[`can`](../can/) (can2040 over PIO), [`mcp2515`](../mcp2515/) (SPI), and
[`mcp2518fd`](../mcp2518fd/) (SPI, classic CAN and CAN FD).

`can_message_t` and `can_filter_t` are the application-facing frame and filter
shapes; `can_id_pack`/`can_id_value`/`can_dlc_to_length` handle the identifier
and length-field arithmetic that gets a CAN frame wrong quietly rather than
loudly. `can_queue_t` is the single-producer/single-consumer byte-FIFO frame
queue `can` uses between its interrupt and the main loop; `mcp2515` does not
need it, since a synchronously-polled SPI controller has no interrupt-context
producer to hand off from.

`can_fd_frame.h` adds the separate `can_fd_message_t` type with up to 64 data
bytes, FD/BRS flags and received ESI metadata. Its byte-length/DLC helpers
validate the discrete FD lengths without implicit padding. Conversion helpers
bridge classic messages; conversion back rejects every FD frame. The existing
classic message type, DLC semantics and queue storage are unchanged. MCP2518FD
uses its hardware FIFOs, so no FD software queue is supplied.

This component has no Pico SDK dependency beyond `pico_stdlib` and is
compiled directly into the host tests — see
`tests/components/can_frame_test.c` and `tests/components/mcp2518fd_frame_test.c`.
See [`can`](../can/README.md)'s "Basic
use" section for the identifier, filter, and queue API in context.
