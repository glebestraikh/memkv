#pragma once

void terminal_disable_echo(void);

void terminal_enable_echo(void);

char* terminal_read_password(const char *prompt);

char* terminal_read_line(const char *prompt);

char* terminal_read_command(const char *prompt);

char* terminal_mask_password(const char *line);
