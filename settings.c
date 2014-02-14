/*
 * Copyright (C) 2012 Drew Walton & Nathan Bass
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include <signal.h>
#include <sys/wait.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "firmware.h"
#include "install.h"
#include "make_ext4fs.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "recovery_ui.h"

#include "extendedcommands.h"
#include "nandroid.h"
#include "mounts.h"
#include "flashutils/flashutils.h"
#include "edify/expr.h"
#include <libgen.h>
#include "mtdutils/mtdutils.h"
#include "bmlutils/bmlutils.h"
#include "cutils/android_reboot.h"

#include "nandroid.h"
#include "settings.h"
#include "iniparse/ini.h"
#include "settingshandler.h"
#include "settingshandler_lang.h"

int UICOLOR0 = 0;
int UICOLOR1 = 0;
int UICOLOR2 = 0;
int UITHEME = 0;

int UI_COLOR_DEBUG = 0;

void show_cot_options_menu() {
    static char* headers[] = { "COT Options",
                                "",
                                NULL
    };

	#define COT_OPTIONS_ITEM_RECDEBUG	0
	#define COT_OPTIONS_ITEM_SETTINGS	1
	#define COT_OPTIONS_ITEM_QUICKFIXES	2

#ifdef BOARD_HAS_QUICKFIXES
	static char* list[4];
	list[0] = "Recovery Debugging";
	list[1] = "COT Settings";
	list[2] = "Quick Fixes";
	list[3] = NULL;
#else
	static char* list[3];
	list[0] = "Recovery Debugging";
	list[1] = "COT Settings";
	list[2] = NULL;
#endif
	for (;;) {
		int chosen_item = get_menu_selection(headers, list, 0, 0);
		switch (chosen_item) {
			case GO_BACK:
				return;
			case COT_OPTIONS_ITEM_RECDEBUG:
				show_recovery_debugging_menu();
				break;
			case COT_OPTIONS_ITEM_SETTINGS:
				show_settings_menu();
				break;
			case COT_OPTIONS_ITEM_QUICKFIXES:
			{
				static char* fixes_headers[3];
				fixes_headers[0] = "Quick Fixes";
				fixes_headers[1] = "\n";
				fixes_headers[2] = NULL;
#ifdef BOARD_NEEDS_RECOVERY_FIX
				static char* fixes_list[2];
				fixes_list[0] = "Fix Recovery Boot Loop";
				fixes_list[1] = NULL;
#else
				static char* fixes_list[1];
				fixes_list[0] = NULL;
#endif
				int chosen_fix = get_menu_selection(fixes_headers, fixes_list, 0, 0);
				switch (chosen_fix) {
					case GO_BACK:
						continue;
					case 0:
#ifdef BOARD_NEEDS_RECOVERY_FIX
						format_root_device("MISC:");
						format_root_device("PERSIST:");
						reboot(RB_AUTOBOOT);
						break;
#else
						break;
#endif
				}
			}
		}
	}
}

void show_recovery_debugging_menu()
{
	static char* headers[] = { "Recovery Debugging",
								"",
								NULL
	};

	static char* list[] = { "Fix Permissions",
							"Report Error",
							"Key Test",
							"Show log",
							"Toggle UI Debugging",
							NULL
	};

	for (;;)
	{
		int chosen_item = get_menu_selection(headers, list, 0, 0);
		if(chosen_item == GO_BACK)
			break;
		switch(chosen_item)
		{
			case 0:
			{
				ensure_path_mounted("/system");
				ensure_path_mounted("/data");
				ui_print("Fixing permissions...\n");
				__system("fix_permissions");
				ui_print("Done!\n");
				break;
			}
			case 1:
				handle_failure(1);
				break;
			case 2:
			{
				ui_print("Outputting key codes.\n");
				ui_print("Go back to end debugging.\n");
				int key;
				int action;
				do
				{
					key = ui_wait_key();
					action = device_handle_key(key, 1);
					ui_print("Key: %d\n", key);
				}
				while (action != GO_BACK);
				break;
			}
			case 3:
				ui_printlogtail(12);
				break;
			case 4:
				toggle_ui_debugging();
				break;
		}
	}
}

char *replace_str(char *str, char *orig, char *rep) {
	static char buffer[4096];
	char *p;
	if(!(p = strstr(str, orig)))
		return str;
		
	strncpy(buffer, str, p-str);
	buffer[p-str] = '\0';
	sprintf(buffer+(p-str), "%s%s", rep, p+strlen(orig));
	return buffer;
}

void show_settings_menu() {
    static char* headers[] = { "COT Settings",
                                "",
                                NULL
    };
    
    #define SETTINGS_ITEM_LANGUAGE      0
    #define SETTINGS_ITEM_THEME         1
    #define SETTINGS_CHOOSE_BACKUP_FMT	2
    #define SETTINGS_ITEM_ORS_REBOOT    3
    #define SETTINGS_ITEM_ORS_WIPE      4
    #define SETTINGS_ITEM_NAND_PROMPT   5
    #define SETTINGS_ITEM_SIGCHECK      6

    static char* list[8];
	
    list[0] = "Language";
    list[1] = "Theme";
    if (backupfmt == 0) {
		list[2] = "Choose Backup Format (currently dup)";
	} else {
		list[2] = "Choose Backup Format (currently tar)";
	}
    if (orsreboot == 1) {
		list[3] = "Disable forced reboots";
	} else {
		list[3] = "Enable forced reboots";
	}
	if (orswipeprompt == 1) {
		list[4] = "Disable wipe prompt";
	} else {
		list[4] = "Enable wipe prompt";
	}
	if (backupprompt == 1) {
		list[5] = "Disable zip flash nandroid prompt";
	} else {
		list[5] = "Enable zip flash nandroid prompt";
	}
    if (signature_check_enabled == 1) {
		list[6] = "Disable md5 signature check";
	} else {
		list[6] = "Enable md5 signature check";
	}
    list[7] = NULL;

    for (;;) {
        int chosen_item = get_menu_selection(headers, list, 0, 0);
        switch (chosen_item) {
            case GO_BACK:
                return;
            case SETTINGS_ITEM_THEME:
            {
                static char* ui_colors[] = {"Hydro (default)",
                                                    "Blood Red",
                                                    "Custom Theme (sdcard)",
                                                    NULL
                };
                static char* ui_header[] = {"COT Theme", "", NULL};

                int ui_color = get_menu_selection(ui_header, ui_colors, 0, 0);
                if(ui_color == GO_BACK)
                    continue;
                else {
                    switch(ui_color) {
                        case 0:
                            currenttheme = "hydro";
                            is_sd_theme = 0;
                            break;
                        case 1:
                            currenttheme = "bloodred";
							is_sd_theme = 0;
                            break;
                        case 2:
                        {
							/*
							currenttheme = "custom";
							is_sd_theme = 1;
							 */
							static char* themeheaders[] = {  "Choose a theme",
                                "",
                                NULL
							};
							char* themefile;
							char* theme = choose_file_menu("/sdcard/cotrecovery/theme/", NULL, themeheaders);
							themefile = replace_str(theme, "/sdcard/cotrecovery/theme/", "");
							themefile = replace_str(themefile, "/", "");
							LOGI("Chosen theme: %s\n", themefile);
							currenttheme = themefile;
							is_sd_theme = 1;
							break;
						}
                    }
                    break;
                }
            }
            case SETTINGS_CHOOSE_BACKUP_FMT:
            {
				static char* cb_fmts[] = {"dup", "tar", NULL};
				static char* cb_header[] = {"Choose Backup Format", "", NULL};
				
				int cb_fmt = get_menu_selection(cb_header, cb_fmts, 0, 0);
				if(cb_fmt == GO_BACK)
					continue;
				else {
					switch(cb_fmt) {
						case 0:
							backupfmt = 0;
							ui_print("Backup format set to dedupe.\n");
							nandroid_switch_backup_handler(0);
							list[2] = "Choose Backup Format (currently dup)";
							break;
						case 1:
							backupfmt = 1;
							ui_print("Backup format set to tar.\n");
							nandroid_switch_backup_handler(1);
							list[2] = "Choose Backup Format (currently tar)";
							break;
					}
					break;
				}
			}
            case SETTINGS_ITEM_ORS_REBOOT:
			{
                if (orsreboot == 1) {
					ui_print("Disabling forced reboots.\n");
					list[3] = "Enable forced reboots";
					orsreboot = 0;
				} else {
					ui_print("Enabling forced reboots.\n");
					list[3] = "Disable forced reboots";
					orsreboot = 1;
				}
                break;
            }
            case SETTINGS_ITEM_ORS_WIPE:
            {
				if (orswipeprompt == 1) {
					ui_print("Disabling wipe prompt.\n");
					list[4] = "Enable wipe prompt";
					orswipeprompt = 0;
				} else {
					ui_print("Enabling wipe prompt.\n");
					list[4] = "Disable wipe prompt";
					orswipeprompt = 1;
				}
				break;
            }
            case SETTINGS_ITEM_NAND_PROMPT:
            {
				if (backupprompt == 1) {
					ui_print("Disabling zip flash nandroid prompt.\n");
					list[5] = "Enable zip flash nandroid prompt";
					backupprompt = 0;
				} else {
					ui_print("Enabling zip flash nandroid prompt.\n");
					list[5] = "Disable zip flash nandroid prompt";
					backupprompt = 1;
				}
				break;
            }
            case SETTINGS_ITEM_SIGCHECK:
            {
				if (signature_check_enabled == 1) {
					ui_print("Disabling md5 signature check.\n");
					list[6] = "Enable md5 signature check";
					signature_check_enabled = 0;
				} else {
					ui_print("Enabling md5 signature check.\n");
					list[6] = "Disable md5 signature check";
					signature_check_enabled = 1;
				}
				break;
			}
            case SETTINGS_ITEM_LANGUAGE:
            {
                static char* lang_list[] = {"English",
#if DEV_BUILD == 1
                                            "Use Custom Language",
#endif
                                            NULL
                };
                static char* lang_headers[] = {"Language", "", NULL};

                int result = get_menu_selection(lang_headers, lang_list, 0, 0);
                if(result == GO_BACK) {
                    continue;
                } else if (result == 0) {
                    language = "en";
                } else if (result == 1) {
                    language = "custom";
                }

                break;
            }
            default:
                return;
        }
        update_cot_settings();
    }
}

void clear_screen() {
	ui_print("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
}
