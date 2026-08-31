/*
 * btstack_config.h - BTstack configuration for the framework's Bluetooth
 * serial console.
 *
 * BTstack will not compile without one of these and the SDK does not supply it,
 * the same arrangement as lwIP's lwipopts.h. Providing it here means linking the
 * component is enough rather than sending people to copy one out of an example
 * and guess which settings matter.
 *
 * Configured for exactly what the console needs: Classic Bluetooth, RFCOMM and
 * SPP, with no BLE, no audio and no mesh. An application wanting more puts its
 * own btstack_config.h earlier on the include path.
 */

#ifndef PICO_FRAMEWORK_BTSTACK_CONFIG_H
#define PICO_FRAMEWORK_BTSTACK_CONFIG_H

/* Classic only. BLE would roughly double the code and is not what a serial
   console wants — see the component README. */
#define ENABLE_CLASSIC

#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP

/*
 * Secure Simple Pairing, so a modern host pairs without a legacy PIN. The
 * console still needs a pairing step; see the README on why it is not
 * disabled.
 */
#define ENABLE_SSP

/* BTstack's own heap comes from these pools rather than malloc, so its memory
   use is bounded and known at link time. One connection is all a console
   needs. */
#define MAX_NR_HCI_CONNECTIONS 1
#define MAX_NR_L2CAP_SERVICES 3
#define MAX_NR_L2CAP_CHANNELS 3
#define MAX_NR_RFCOMM_MULTIPLEXERS 1
#define MAX_NR_RFCOMM_SERVICES 1
#define MAX_NR_RFCOMM_CHANNELS 1
#define MAX_NR_BNEP_SERVICES 0
#define MAX_NR_BNEP_CHANNELS 0
#define MAX_NR_HFP_CONNECTIONS 0
#define MAX_NR_WHITELIST_ENTRIES 0
#define MAX_NR_SM_LOOKUP_ENTRIES 0
#define MAX_NR_SERVICE_RECORD_ITEMS 4
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 0
#define MAX_NR_AVDTP_CONNECTIONS 0
#define MAX_NR_AVRCP_CONNECTIONS 0
#define MAX_NR_LE_DEVICE_DB_ENTRIES 0

/*
 * One RFCOMM packet. 1021 is what an unfragmented L2CAP payload allows and is
 * far more than a console line, but a short buffer here would fragment every
 * reply.
 */
#define HCI_ACL_PAYLOAD_SIZE 1021
#define HCI_INCOMING_PRE_BUFFER_SIZE 4

/*
 * Required by the CYW43 HCI transport, which refuses to compile without them.
 * It prepends a four-byte header to every outgoing packet and needs the buffer
 * reserved in front, and it moves ACL data in four-byte units.
 *
 * Getting these two wrong is a #error rather than a subtle fault, which is the
 * good case — but they are also the sort of thing nobody should have to
 * discover, so they are set here rather than left to each application.
 */
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

/* Link keys, so a paired host does not have to pair again. Kept in flash by
   pico_btstack_flash_bank. */
#define NVM_NUM_LINK_KEYS 4
#define NVM_NUM_DEVICE_DB_ENTRIES 0

#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

#endif /* PICO_FRAMEWORK_BTSTACK_CONFIG_H */
