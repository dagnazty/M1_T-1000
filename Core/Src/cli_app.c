/*
FreeRTOS+CLI is released under the following MIT license.

Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//https://github.com/FreeRTOS/FreeRTOS/tree/main/FreeRTOS-Plus/Source/FreeRTOS-Plus-CLI

#ifndef CLI_COMMANDS_H
#define CLI_COMMANDS_H

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "stdbool.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "m1_log_debug.h"
#include "m1_cli.h"
#include "m1_link.h"
#include "m1_compile_cfg.h"
#ifdef M1_APP_ESPNOW_LINK_ENABLE
#include "m1_esp_link.h"
#endif

#define MAX_INPUT_LENGTH 		64
#define USING_VS_CODE_TERMINAL 	0
#define USING_OTHER_TERMINAL 	1 // e.g. Putty, TerraTerm

char cOutputBuffer[configCOMMAND_INT_MAX_OUTPUT_SIZE], pcInputString[MAX_INPUT_LENGTH];
extern const CLI_Command_Definition_t xCommandList[];
int8_t cRxedChar;
const char * cli_prompt = "\r\ncli> ";
/* CLI escape sequences*/
uint8_t backspace[] = "\b \b";
uint8_t backspace_tt[] = " \b";

BaseType_t cmd_clearScreen(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_clearScreen_help(void);
#ifdef M1_APP_LINK_ENABLE
BaseType_t cmd_m1_link(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_m1_link_help(void);
#endif
#ifdef M1_APP_ESPNOW_LINK_ENABLE
BaseType_t cmd_m1_espnow(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params);
BaseType_t cmd_m1_espnow_help(void);
#endif
void vRegisterCLICommands(void);
void cliWrite(const char *str);
void handleNewline(const char *const pcInputString, char *cOutputBuffer, uint8_t *cInputIndex);
void handleBackspace(uint8_t *cInputIndex, char *pcInputString);
void handleCharacterInput(uint8_t *cInputIndex, char *pcInputString);

const CLI_Command_Definition_t xCommandList[] = {
    {
        .pcCommand = "cls", /* The command string to type. */
        .pcHelpString = "cls:\r\n Clears screen\r\n\r\n",
        .pxCommandInterpreter = cmd_clearScreen, /* The function to run. */
		.pxCommandHelper = cmd_clearScreen_help, /* Help for the function */
        .cExpectedNumberOfParameters = 0 /* No parameters are expected. */
    },
    {
        .pcCommand = "mtest", /* The command string to type. */
        .pcHelpString = "mtest:\r\nThis is the multi-purpose test command\r\n\r\n",
        .pxCommandInterpreter = cmd_m1_mtest, /* The function to run. */
		.pxCommandHelper = cmd_m1_mtest_help, /* Help for the function. */
        .cExpectedNumberOfParameters = -1 /* variable parameters are expected. */
    },
#ifdef M1_APP_LINK_ENABLE
    {
        .pcCommand = "link", /* M1 Link device-to-device sub-GHz spike (Phase 0). */
        .pcHelpString = "link:\r\n M1 Link sub-GHz spike. Usage:\r\n"
                        "  link info            radio bring-up + part info\r\n"
                        "  link beacon [n] [ms] TX n packets ms apart (def 20, 500)\r\n"
                        "  link listen [s]      RX for s seconds (def 30)\r\n"
                        "  link rxdiag [s]      RX diagnostics: state/rssi/modem (def 15)\r\n"
                        "  link id              show this unit's short id\r\n"
                        "  link send <id> <txt> reliable send to id (hex; FFFF=bcast)\r\n"
                        "  link recv [s]        receive+auto-ACK messages for s s (def 30)\r\n"
                        "  link scan [s]        discover peers via HELLO beacon (def 20)\r\n"
                        "  link ping <id>       ping peer: round-trip + RSSI\r\n"
                        "  link locate <id>     make peer beep + flash LED\r\n"
                        "  link sendfile <id> <path>  send an SD file to a peer\r\n"
                        "  link trigger <id> <sub|badusb|badbt|ir> <path>\r\n\r\n",
        .pxCommandInterpreter = cmd_m1_link, /* The function to run. */
        .pxCommandHelper = cmd_m1_link_help, /* Help for the function. */
        .cExpectedNumberOfParameters = -1 /* variable parameters are expected. */
    },
#endif
#ifdef M1_APP_ESPNOW_LINK_ENABLE
    {
        .pcCommand = "espnow", /* M1 Link over ESP32 (ESP-NOW) remote-trigger spike (Phase 0). */
        .pcHelpString = "espnow:\r\n M1 Link over ESP-NOW. Usage:\r\n"
                        "  espnow on [ch]        bring ESP-NOW up (channel 1-13, def 1)\r\n"
                        "  espnow off            bring ESP-NOW down\r\n"
                        "  espnow info           state: enabled, channel, own MAC, key\r\n"
                        "  espnow key <pass>     set shared AES passphrase ('' clears)\r\n"
                        "  espnow scan [s]       broadcast HELLO, list peers (def 3)\r\n"
                        "  espnow pair [mac]     add paired peer (no arg = list)\r\n"
                        "  espnow trig <mac> <badusb> <name>  send an ACK'd trigger\r\n"
                        "  espnow listen [s]     run received BadUSB triggers (def 30)\r\n\r\n",
        .pxCommandInterpreter = cmd_m1_espnow, /* The function to run. */
        .pxCommandHelper = cmd_m1_espnow_help, /* Help for the function. */
        .cExpectedNumberOfParameters = -1 /* variable parameters are expected. */
    },
#endif
    {
        .pcCommand = NULL /* simply used as delimeter for end of array*/
    }
};


/*============================================================================*/
/*
 * Command Line Interface handler task
 *
 */
/*============================================================================*/
void vCommandConsoleTask(void *pvParameters)
{
    uint8_t cInputIndex = 0; // simply used to keep track of the index of the input string
    uint32_t receivedValue; // used to store the received value from the notification

    UNUSED(pvParameters);
    vRegisterCLICommands();

    for (;;)
    {
        xTaskNotifyWait(pdFALSE,    // Don't clear bits on entry
                                  0,  // Clear all bits on exit
                                  &receivedValue, // Receives the notification value
                                  portMAX_DELAY); // Wait indefinitely
        //echo recevied char
        cRxedChar = receivedValue & 0xFF;
        cliWrite((char *)&cRxedChar);
        if (cRxedChar == '\r' || cRxedChar == '\n')
        {
            // user pressed enter, process the command
            handleNewline(pcInputString, cOutputBuffer, &cInputIndex);
        }
        else
        {
            // user pressed a character add it to the input string
            handleCharacterInput(&cInputIndex, pcInputString);
        }
    }
} // void vCommandConsoleTask(void *pvParameters)



/*============================================================================*/
/*
 * CLI command: Clear Screen
 *
 */
/*============================================================================*/
BaseType_t cmd_clearScreen(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    /* Remove compile time warnings about unused parameters, and check the
	write buffer is not NULL.  NOTE - for simplicity, this example assumes the
	write buffer length is adequate, so does not check for buffer overflows. */
    (void)pcCommandString;
    (void)xWriteBufferLen;
    memset(pcWriteBuffer, 0x00, xWriteBufferLen);
    printf("\033[2J\033[1;1H");
    return pdFALSE;
} // BaseType_t cmd_clearScreen(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)



/*============================================================================*/
/*
 * Hlep for the CLI command: Clear Screen
 *
 */
/*============================================================================*/
BaseType_t cmd_clearScreen_help(void)
{
    return pdFALSE;
} // BaseType_t cmd_clearScreen_help(void)



#ifdef M1_APP_LINK_ENABLE
/*============================================================================*/
/*
 * CLI command: M1 Link device-to-device sub-GHz spike (Phase 0)
 *
 *   link info            radio bring-up + part info
 *   link beacon [n] [ms] transmit n fixed packets, ms apart
 *   link listen [s]      receive for s seconds
 *
 * Two-device test: run "link listen" on one unit, "link beacon" on the other.
 */
/*============================================================================*/
BaseType_t cmd_m1_link(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    const char *sub;
    BaseType_t sub_len;

    (void)xWriteBufferLen;
    memset(pcWriteBuffer, 0x00, xWriteBufferLen);

    if ( num_of_params == 0 )
    {
        cmd_m1_link_help();
        return pdFALSE;
    }

    sub = FreeRTOS_CLIGetParameter(pcCommandString, 1, &sub_len);
    if ( sub == NULL )
    {
        cmd_m1_link_help();
        return pdFALSE;
    }

    if ( strncmp(sub, "info", sub_len) == 0 )
    {
        m1_link_spike_info();
    }
    else if ( strncmp(sub, "beacon", sub_len) == 0 )
    {
        const char *p_n  = FreeRTOS_CLIGetParameter(pcCommandString, 2, &sub_len);
        const char *p_ms = FreeRTOS_CLIGetParameter(pcCommandString, 3, &sub_len);
        uint32_t n  = (p_n  != NULL) ? (uint32_t)strtol(p_n,  NULL, 10) : 20u;
        uint32_t ms = (p_ms != NULL) ? (uint32_t)strtol(p_ms, NULL, 10) : 500u;
        m1_link_spike_beacon(n, ms);
    }
    else if ( strncmp(sub, "listen", sub_len) == 0 )
    {
        const char *p_s = FreeRTOS_CLIGetParameter(pcCommandString, 2, &sub_len);
        uint32_t s = (p_s != NULL) ? (uint32_t)strtol(p_s, NULL, 10) : 30u;
        m1_link_spike_listen(s);
    }
    else if ( strncmp(sub, "rxdiag", sub_len) == 0 )
    {
        const char *p_s = FreeRTOS_CLIGetParameter(pcCommandString, 2, &sub_len);
        uint32_t s = (p_s != NULL) ? (uint32_t)strtol(p_s, NULL, 10) : 15u;
        m1_link_spike_rxdiag(s);
    }
    else if ( strncmp(sub, "id", sub_len) == 0 )
    {
        printf("M1 Link: my id = %04X\r\n", (unsigned)m1_link_my_id());
    }
    else if ( strncmp(sub, "send", sub_len) == 0 )
    {
        BaseType_t l2, l3;
        const char *p_dst = FreeRTOS_CLIGetParameter(pcCommandString, 2, &l2);
        const char *p_txt = FreeRTOS_CLIGetParameter(pcCommandString, 3, &l3);
        if ( p_dst == NULL || p_txt == NULL )
        {
            printf("usage: link send <dsthex|FFFF> <text>\r\n");
        }
        else
        {
            uint16_t dst = (uint16_t)strtol(p_dst, NULL, 16);
            /* text is the remainder of the command line from param 3 onward */
            m1_link_spike_send(dst, p_txt);
        }
    }
    else if ( strncmp(sub, "recv", sub_len) == 0 )
    {
        const char *p_s = FreeRTOS_CLIGetParameter(pcCommandString, 2, &sub_len);
        uint32_t s = (p_s != NULL) ? (uint32_t)strtol(p_s, NULL, 10) : 30u;
        m1_link_spike_recv(s);
    }
    else if ( strncmp(sub, "ping", sub_len) == 0 )
    {
        BaseType_t l2;
        const char *p_dst = FreeRTOS_CLIGetParameter(pcCommandString, 2, &l2);
        if ( p_dst == NULL ) printf("usage: link ping <dsthex>\r\n");
        else m1_link_spike_ping((uint16_t)strtol(p_dst, NULL, 16));
    }
    else if ( strncmp(sub, "locate", sub_len) == 0 )
    {
        BaseType_t l2;
        const char *p_dst = FreeRTOS_CLIGetParameter(pcCommandString, 2, &l2);
        if ( p_dst == NULL ) printf("usage: link locate <dsthex>\r\n");
        else m1_link_spike_locate((uint16_t)strtol(p_dst, NULL, 16));
    }
    else if ( strncmp(sub, "sendfile", sub_len) == 0 )
    {
        BaseType_t l2, l3;
        const char *p_dst  = FreeRTOS_CLIGetParameter(pcCommandString, 2, &l2);
        const char *p_path = FreeRTOS_CLIGetParameter(pcCommandString, 3, &l3);
        if ( p_dst == NULL || p_path == NULL )
            printf("usage: link sendfile <dsthex> <path>\r\n");
        else
            m1_link_spike_sendfile((uint16_t)strtol(p_dst, NULL, 16), p_path);
    }
    else if ( strncmp(sub, "trigger", sub_len) == 0 )
    {
        BaseType_t l2, l3, l4;
        const char *p_dst  = FreeRTOS_CLIGetParameter(pcCommandString, 2, &l2);
        const char *p_type = FreeRTOS_CLIGetParameter(pcCommandString, 3, &l3);
        const char *p_name = FreeRTOS_CLIGetParameter(pcCommandString, 4, &l4);
        if ( p_dst == NULL || p_type == NULL || p_name == NULL )
            printf("usage: link trigger <dsthex> <sub|badusb|badbt|ir> <path>\r\n");
        else
        {
            uint8_t ty = M1_LINK_TRIG_SUB;
            if      ( strncmp(p_type, "badusb", l3) == 0 ) ty = M1_LINK_TRIG_BADUSB;
            else if ( strncmp(p_type, "badbt",  l3) == 0 ) ty = M1_LINK_TRIG_BADBT;
            else if ( strncmp(p_type, "ir",     l3) == 0 ) ty = M1_LINK_TRIG_IR;
            m1_link_spike_trigger((uint16_t)strtol(p_dst, NULL, 16), ty, p_name);
        }
    }
    else if ( strncmp(sub, "scan", sub_len) == 0 )
    {
        const char *p_s = FreeRTOS_CLIGetParameter(pcCommandString, 2, &sub_len);
        uint32_t s = (p_s != NULL) ? (uint32_t)strtol(p_s, NULL, 10) : 20u;
        m1_link_spike_scan(s);
    }
    else if ( strncmp(sub, "cfg", sub_len) == 0 )
    {
        m1_link_spike_info();   /* prints id/callsign/channel/power/enc */
    }
    else if ( strncmp(sub, "key", sub_len) == 0 )
    {
        BaseType_t l2;
        const char *p = FreeRTOS_CLIGetParameter(pcCommandString, 2, &l2);
        m1_link_cfg_set(NULL, (p != NULL) ? p : "", -1, -1, false);
        printf("M1 Link: encryption %s\r\n", m1_link_encrypted() ? "ON" : "off");
    }
    else if ( strncmp(sub, "chan", sub_len) == 0 )
    {
        const char *p = FreeRTOS_CLIGetParameter(pcCommandString, 2, &sub_len);
        int ch = (p != NULL) ? (int)strtol(p, NULL, 10) : 0;
        m1_link_cfg_set(NULL, NULL, ch, -1, false);
        printf("M1 Link: channel set\r\n");
    }
    else if ( strncmp(sub, "name", sub_len) == 0 )
    {
        BaseType_t l2;
        const char *p = FreeRTOS_CLIGetParameter(pcCommandString, 2, &l2);
        if ( p != NULL ) { m1_link_cfg_set(p, NULL, -1, -1, false);
                           printf("M1 Link: callsign set\r\n"); }
    }
    else if ( strncmp(sub, "save", sub_len) == 0 )
    {
        m1_link_cfg_save();
        printf("M1 Link: config saved\r\n");
    }
    else
    {
        cmd_m1_link_help();
    }

    return pdFALSE;
} // BaseType_t cmd_m1_link(...)



/*============================================================================*/
/*
 * Help for the CLI command: M1 Link
 */
/*============================================================================*/
BaseType_t cmd_m1_link_help(void)
{
    printf("\r\nM1 Link (Phase 0 spike) — sub-GHz 915 MHz FSK packet link\r\n");
    printf("  link info            radio bring-up + part info\r\n");
    printf("  link beacon [n] [ms] TX n packets, ms apart (default 20, 500)\r\n");
    printf("  link listen [s]      RX for s seconds (default 30)\r\n");
    printf("  link rxdiag [s]      RX diagnostics: state/rssi/modem (default 15)\r\n");
    printf("  link id              show this unit's short id\r\n");
    printf("  link send <id> <txt> reliable send to id (hex; FFFF=broadcast)\r\n");
    printf("  link recv [s]        receive + auto-ACK for s seconds (default 30)\r\n");
    printf("  link scan [s]        discover peers via HELLO beacon (default 20)\r\n");
    printf("  link ping <id>       ping peer: round-trip + RSSI\r\n");
    printf("  link locate <id>     make peer beep + flash LED\r\n");
    printf("  link sendfile <id> <path>  send an SD file to a peer\r\n");
    printf("  link trigger <id> <sub|badusb|badbt|ir> <path>\r\n");
    printf("Phase0: 'link listen'+'link beacon'.  Phase1/2: 'link recv'+'link send', 'link scan'.\r\n\r\n");
    return pdFALSE;
} // BaseType_t cmd_m1_link_help(void)
#endif /* M1_APP_LINK_ENABLE */


#ifdef M1_APP_ESPNOW_LINK_ENABLE
/*============================================================================*/
/*
 * CLI command: M1 Link over ESP32 (ESP-NOW) remote-trigger spike (Phase 0)
 *
 * Two-device test (both M1s on USB):
 *   Unit B:  espnow on 1     then  espnow listen 60
 *   Unit A:  espnow on 1     then  espnow info   (read A/B MACs)
 *   Unit A:  espnow trig <B_mac> badusb <name.txt>
 *   -> B prints +ESPNOWRX and runs 0:/BadUSB/<name.txt>.
 */
/*============================================================================*/
BaseType_t cmd_m1_espnow(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString, uint8_t num_of_params)
{
    const char *sub;
    BaseType_t sub_len;

    (void)xWriteBufferLen;
    memset(pcWriteBuffer, 0x00, xWriteBufferLen);

    if ( num_of_params == 0 )
    {
        cmd_m1_espnow_help();
        return pdFALSE;
    }

    sub = FreeRTOS_CLIGetParameter(pcCommandString, 1, &sub_len);
    if ( sub == NULL )
    {
        cmd_m1_espnow_help();
        return pdFALSE;
    }

    if ( strncmp(sub, "on", sub_len) == 0 )
    {
        const char *p_ch = FreeRTOS_CLIGetParameter(pcCommandString, 2, &sub_len);
        uint8_t ch = (p_ch != NULL) ? (uint8_t)strtol(p_ch, NULL, 10) : 1u;
        m1_esp_link_enable(ch);
    }
    else if ( strncmp(sub, "off", sub_len) == 0 )
    {
        m1_esp_link_disable();
    }
    else if ( strncmp(sub, "info", sub_len) == 0 )
    {
        m1_esp_link_info();
    }
    else if ( strncmp(sub, "key", sub_len) == 0 )
    {
        BaseType_t l2;
        const char *p = FreeRTOS_CLIGetParameter(pcCommandString, 2, &l2);
        char key[64];
        if ( p != NULL )
        {
            /* FreeRTOS_CLIGetParameter returns a pointer + length into the
             * command line, NOT a null-terminated token — terminate it. */
            size_t kl = ((size_t)l2 < sizeof(key)) ? (size_t)l2 : sizeof(key) - 1;
            memcpy(key, p, kl);
            key[kl] = '\0';
        }
        m1_esp_link_key(p != NULL ? key : NULL);
    }
    else if ( strncmp(sub, "scan", sub_len) == 0 )
    {
        const char *p_s = FreeRTOS_CLIGetParameter(pcCommandString, 2, &sub_len);
        uint32_t s = (p_s != NULL) ? (uint32_t)strtol(p_s, NULL, 10) : 3u;
        m1_esp_link_scan(s);
    }
    else if ( strncmp(sub, "pair", sub_len) == 0 )
    {
        BaseType_t l2;
        const char *p_mac = FreeRTOS_CLIGetParameter(pcCommandString, 2, &l2);
        if ( p_mac == NULL ) m1_esp_link_pairs();   /* no arg = list */
        else                 m1_esp_link_pair(p_mac);
    }
    else if ( strncmp(sub, "trig", sub_len) == 0 )
    {
        BaseType_t l2, l3, l4;
        const char *p_mac  = FreeRTOS_CLIGetParameter(pcCommandString, 2, &l2);
        const char *p_type = FreeRTOS_CLIGetParameter(pcCommandString, 3, &l3);
        const char *p_name = FreeRTOS_CLIGetParameter(pcCommandString, 4, &l4);
        if ( p_mac == NULL || p_type == NULL || p_name == NULL )
        {
            printf("usage: espnow trig <mac> <badusb> <name>\r\n");
        }
        else
        {
            /* FreeRTOS_CLIGetParameter returns a pointer + length into the
             * command line, NOT a null-terminated token — copy each out. */
            char mac[18], name[64];
            size_t ml = ((size_t)l2 < sizeof(mac))  ? (size_t)l2 : sizeof(mac)  - 1;
            size_t nl = ((size_t)l4 < sizeof(name)) ? (size_t)l4 : sizeof(name) - 1;
            memcpy(mac,  p_mac,  ml); mac[ml]  = '\0';
            memcpy(name, p_name, nl); name[nl] = '\0';

            uint8_t ty = M1_ESPNOW_PTYPE_BADUSB;
            if      ( strncmp(p_type, "sub",    l3) == 0 ) ty = M1_ESPNOW_PTYPE_SUB;
            else if ( strncmp(p_type, "ir",     l3) == 0 ) ty = M1_ESPNOW_PTYPE_IR;
            else if ( strncmp(p_type, "badusb", l3) == 0 ) ty = M1_ESPNOW_PTYPE_BADUSB;
            m1_esp_link_trigger(mac, ty, name);
        }
    }
    else if ( strncmp(sub, "send", sub_len) == 0 )
    {
        BaseType_t l2, l3, l4;
        const char *p_mac  = FreeRTOS_CLIGetParameter(pcCommandString, 2, &l2);
        const char *p_file = FreeRTOS_CLIGetParameter(pcCommandString, 3, &l3);
        const char *p_name = FreeRTOS_CLIGetParameter(pcCommandString, 4, &l4); /* optional */
        if ( p_mac == NULL || p_file == NULL )
        {
            printf("usage: espnow send <mac> <name|path> [remotename]\r\n");
        }
        else
        {
            char mac[18], file[64], path[128], name[64];
            size_t ml = ((size_t)l2 < sizeof(mac))  ? (size_t)l2 : sizeof(mac)  - 1;
            size_t fl = ((size_t)l3 < sizeof(file)) ? (size_t)l3 : sizeof(file) - 1;
            memcpy(mac,  p_mac,  ml); mac[ml]  = '\0';
            memcpy(file, p_file, fl); file[fl] = '\0';

            /* Infer payload type from the extension: .sub -> Sub-GHz, .ir -> IR,
             * otherwise BadUSB. A bare name is read from that type's folder; a
             * path with '/' is used as-is. */
            uint8_t ty; const char *dir;
            size_t  flen = strlen(file);
            if ( flen >= 4 && strcmp(file + flen - 4, ".sub") == 0 )
                { ty = M1_ESPNOW_PTYPE_SUB;    dir = "0:/SUBGHZ"; }
            else if ( flen >= 3 && strcmp(file + flen - 3, ".ir") == 0 )
                { ty = M1_ESPNOW_PTYPE_IR;     dir = "0:/IR"; }
            else
                { ty = M1_ESPNOW_PTYPE_BADUSB; dir = "0:/BadUSB"; }

            if ( strchr(file, '/') ) snprintf(path, sizeof(path), "%s", file);
            else                     snprintf(path, sizeof(path), "%s/%s", dir, file);

            if ( p_name )
            {
                size_t nl = ((size_t)l4 < sizeof(name)) ? (size_t)l4 : sizeof(name) - 1;
                memcpy(name, p_name, nl); name[nl] = '\0';
            }
            else
            {
                const char *base = strrchr(path, '/');
                base = base ? base + 1 : path;
                strncpy(name, base, sizeof(name) - 1); name[sizeof(name) - 1] = '\0';
            }
            bool ok = m1_esp_link_send_file_ok(mac, ty, path, name);
            printf("ESP-NOW: send %s -> %s\r\n", name, ok ? "OK" : "FAILED");
        }
    }
    else if ( strncmp(sub, "listen", sub_len) == 0 )
    {
        const char *p_s = FreeRTOS_CLIGetParameter(pcCommandString, 2, &sub_len);
        uint32_t s = (p_s != NULL) ? (uint32_t)strtol(p_s, NULL, 10) : 30u;
        m1_esp_link_listen(s);
    }
    else
    {
        cmd_m1_espnow_help();
    }

    return pdFALSE;
} // BaseType_t cmd_m1_espnow(...)


/*============================================================================*/
/*
 * Help for the CLI command: M1 Link over ESP-NOW
 */
/*============================================================================*/
BaseType_t cmd_m1_espnow_help(void)
{
    printf("\r\nM1 Link over ESP-NOW (Phase 0 spike) — 2.4 GHz remote trigger\r\n");
    printf("  espnow on [ch]        bring ESP-NOW up (channel 1-13, default 1)\r\n");
    printf("  espnow off            bring ESP-NOW down\r\n");
    printf("  espnow info           state: enabled, channel, own MAC, key-set\r\n");
    printf("  espnow key <pass>     set shared AES passphrase ('' clears)\r\n");
    printf("  espnow scan [s]       broadcast HELLO, list peers (default 3)\r\n");
    printf("  espnow pair [mac]     add paired peer (no arg = list paired)\r\n");
    printf("  espnow trig <mac> <badusb> <name>  trigger an EXISTING file on a peer\r\n");
    printf("  espnow send <mac> <name|path> [rn]  transfer a payload + run it\r\n");
    printf("  espnow listen [s]     receive: reassemble + run triggers (default 30)\r\n");
    printf("Encrypted-only: set same 'key' + 'pair' on both; wrong/no key is dropped.\r\n\r\n");
    return pdFALSE;
} // BaseType_t cmd_m1_espnow_help(void)
#endif /* M1_APP_ESPNOW_LINK_ENABLE */



/*============================================================================*/
/*
 * Register the list of commands
 *
 */
/*============================================================================*/
void vRegisterCLICommands(void)
{
    //itterate through the list of commands and register them
    for (int i = 0; xCommandList[i].pcCommand != NULL; i++)
    {
        FreeRTOS_CLIRegisterCommand(&xCommandList[i]);
    }
} // void vRegisterCLICommands(void)




/*============================================================================*/
/*
 * Write to CLI UART
 *
 */
/*============================================================================*/
void cliWrite(const char *str)
{
   printf("%s", str);
   fflush(stdout);
} // void cliWrite(const char *str)




/*============================================================================*/
/*
 * Handle the CR + LF from the input CLI command
 *
 */
/*============================================================================*/
void handleNewline(const char *const pcInputString, char *cOutputBuffer, uint8_t *cInputIndex)
{
    cliWrite("\r\n");

    BaseType_t xMoreDataToFollow;
    do
    {
        xMoreDataToFollow = FreeRTOS_CLIProcessCommand(pcInputString, cOutputBuffer, configCOMMAND_INT_MAX_OUTPUT_SIZE);
        cliWrite(cOutputBuffer);
        *cOutputBuffer = 0x00; // Clear string after use
    } while (xMoreDataToFollow != pdFALSE);

    cliWrite(cli_prompt);
    *cInputIndex = 0;
    memset((void*)pcInputString, 0x00, MAX_INPUT_LENGTH);
} // void handleNewline(const char *const pcInputString, char *cOutputBuffer, uint8_t *cInputIndex)




/*============================================================================*/
/*
 * Handle the backspace from the input CLI command
 *
 */
/*============================================================================*/
void handleBackspace(uint8_t *cInputIndex, char *pcInputString)
{
    if (*cInputIndex > 0)
    {
        (*cInputIndex)--;
        pcInputString[*cInputIndex] = '\0';

#if USING_VS_CODE_TERMINAL
        cliWrite((char *)backspace);
#elif USING_OTHER_TERMINAL
        cliWrite((char *)backspace_tt);
#endif
    }
    else
    {
#if USING_OTHER_TERMINAL
        uint8_t right[] = "\x1b\x5b\x43";
        cliWrite((char *)right);
#endif
    }
} // void handleBackspace(uint8_t *cInputIndex, char *pcInputString)



/*============================================================================*/
/*
 * Handle a character from the input CLI command
 *
 */
/*============================================================================*/
void handleCharacterInput(uint8_t *cInputIndex, char *pcInputString)
{
    if (cRxedChar == '\r')
    {
        return;
    }
    else if (cRxedChar == (uint8_t)0x08 || cRxedChar == (uint8_t)0x7F)
    {
        handleBackspace(cInputIndex, pcInputString);
    }
    else
    {
        if (*cInputIndex < MAX_INPUT_LENGTH)
        {
            pcInputString[*cInputIndex] = cRxedChar;
            (*cInputIndex)++;
        }
    }
} // void handleCharacterInput(uint8_t *cInputIndex, char *pcInputString)

#endif /* CLI_COMMANDS_H */
