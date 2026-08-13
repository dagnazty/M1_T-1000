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
