/* SPDX-License-Identifier: Apache-2.0 */

#ifndef UF2_INDICATOR_H
#define UF2_INDICATOR_H

enum indicator_state {
    INDICATOR_UNPLUGGED = 0, /* waiting for USB host, slow breathing  */
    INDICATOR_PLUGGED,       /* host enumerated, faster breathing     */
    INDICATOR_WRITING,       /* UF2 write in flight, hard fast blink  */
    INDICATOR_FINISHED,      /* write done, solid until reboot        */
};

void indicator_set(enum indicator_state state);

#endif
