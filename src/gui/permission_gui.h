#pragma once

/* Show a UAC-style permission prompt over the current desktop.
   Darkens the screen, draws a centered dialog, and blocks until the user
   either enters the correct admin PIN or cancels.

   action_desc: short phrase describing what is being authorized,
                e.g. "add a new user" or "install a developer app".

   Returns 1 if the correct admin PIN was entered, 0 if cancelled. */
int permission_prompt_run(const char *action_desc);
