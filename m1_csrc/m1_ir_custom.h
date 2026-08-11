/* See COPYING.txt for license details. */

/*
*
* m1_ir_custom.h
*
* Custom (user-built) IR remotes: create a named remote, learn+name buttons,
* replay from a scrolling button list, and edit (rename/delete) buttons.
*
* M1 Project
*
*/

#ifndef M1_IR_CUSTOM_H_
#define M1_IR_CUSTOM_H_

#include "m1_compile_cfg.h"

#ifdef M1_APP_FILE_IMPORT_ENABLE

/* Main entry point — "Custom Remotes" Infrared submenu item. */
void ir_custom_run(void);

#endif /* M1_APP_FILE_IMPORT_ENABLE */

#endif /* M1_IR_CUSTOM_H_ */
