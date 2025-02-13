/*
  funcs.c
  Misc functions for iGame

  Copyright (c) 2019, Emmanuel Vasilakis and contributors

  This file is part of iGame.

  iGame is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  iGame is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with iGame. If not, see <http://www.gnu.org/licenses/>.
*/

/* MUI */
#include <libraries/mui.h>
#include <mui/Guigfx_mcc.h>
#include <mui/TextEditor_mcc.h>

/* Prototypes */
#include <clib/alib_protos.h>
#include <proto/lowlevel.h>
#include <proto/wb.h>

#if defined(__amigaos4__)
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#else
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <clib/icon_protos.h>
#include <clib/muimaster_protos.h>
#include <clib/graphics_protos.h>
#endif

/* System */
#if defined(__amigaos4__)
#include <dos/obsolete.h>
#define ASL_PRE_V38_NAMES
#endif

/* ANSI C */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define iGame_NUMBERS
#include "iGame_strings.h"

#include "iGameGUI.h"
#include "iGameExtern.h"
#include "strfuncs.h"
#include "fsfuncs.h"
#include "funcs.h"

extern struct ObjApp* app;
extern struct Library *GfxBase;
extern struct Library *IconBase;
extern struct Library *IntuitionBase;
extern char* executable_name;

/* global variables */
int total_hidden = 0;
int showing_hidden = 0;
int total_games;
int no_of_genres;
char* game_tooltypes;
char fname[255];
int IntroPic = 0;
int wbrun = 0;
const int MAX_PATH_SIZE = 255;

/* function definitions */
// int get_genre(char* title, char* genre);
static void follow_thread(BPTR lock, int tab_level);
static void refresh_list(int check_exists);
static int hex2dec(char* hexin);
static int check_dup_title(char* title);
static void check_for_wbrun();
static void list_show_favorites(char* str);

/* structures */
struct EasyStruct msgbox;

games_list *item_games = NULL, *games = NULL;
repos_list *item_repos = NULL, *repos = NULL;
genres_list *item_genres = NULL, *genres = NULL;
igame_settings *current_settings = NULL;

void status_show_total(void)
{
	char helper[200];
	set(app->LV_GamesList, MUIA_List_Quiet, FALSE);
	sprintf(helper, (const char*)GetMBString(MSG_TotalNumberOfGames), total_games);
	set(app->TX_Status, MUIA_Text_Contents, helper);
}

void add_games_to_listview(void)
{
	total_games = 0;
	for (item_games = games; item_games != NULL; item_games = item_games->next)
	{
		if (item_games->hidden != 1 && item_games->deleted != 1)
		{
			total_games++;
			DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title, MUIV_List_Insert_Sorted);
		}
	}
	status_show_total();
}

static void apply_settings()
{
	if (current_settings == NULL)
		return;

	set(app->CH_Screenshots, MUIA_Selected, current_settings->hide_screenshots);
	set(app->CH_NoGuiGfx, MUIA_Selected, current_settings->no_guigfx);

	if (current_settings->screenshot_width == 160 && current_settings->screenshot_height == 128)
	{
		set(app->CY_ScreenshotSize, MUIA_Cycle_Active, 0);
		set(app->STR_Width, MUIA_Disabled, TRUE);
		set(app->STR_Height, MUIA_Disabled, TRUE);
	}
	else if (current_settings->screenshot_width == 320 && current_settings->screenshot_height == 256)
	{
		set(app->CY_ScreenshotSize, MUIA_Cycle_Active, 1);
		set(app->STR_Width, MUIA_Disabled, TRUE);
		set(app->STR_Height, MUIA_Disabled, TRUE);
	}
	else
	{
		set(app->CY_ScreenshotSize, MUIA_Cycle_Active, 2);
		set(app->STR_Width, MUIA_Disabled, FALSE);
		set(app->STR_Height, MUIA_Disabled, FALSE);
		set(app->STR_Width, MUIA_String_Integer, current_settings->screenshot_width);
		set(app->STR_Height, MUIA_String_Integer, current_settings->screenshot_height);
	}

	set(app->RA_TitlesFrom, MUIA_Radio_Active, current_settings->titles_from_dirs);
	if (current_settings->titles_from_dirs)
	{
		set(app->CH_SmartSpaces, MUIA_Disabled, FALSE);
		set(app->CH_SmartSpaces, MUIA_Selected, current_settings->no_smart_spaces);
	}
	else
	{
		set(app->CH_SmartSpaces, MUIA_Disabled, TRUE);
		set(app->CH_SmartSpaces, MUIA_Disabled, TRUE);
	}

	set(app->CH_SaveStatsOnExit, MUIA_Selected, current_settings->save_stats_on_exit);
	set(app->CH_FilterUseEnter, MUIA_Selected, current_settings->filter_use_enter);
	set(app->CH_HideSidepanel, MUIA_Selected, current_settings->hide_side_panel);
	set(app->CH_StartWithFavorites, MUIA_Selected, current_settings->start_with_favorites);
}

igame_settings *load_settings(const char* filename)
{
	const int buffer_size = 512;
	STRPTR file_line = malloc(buffer_size * sizeof(char));
	if (file_line == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return NULL;
	}

	if (current_settings != NULL)
	{
		free(current_settings);
		current_settings = NULL;
	}
	current_settings = (igame_settings *)calloc(1, sizeof(igame_settings));

	// TODO: Maybe it would be a good idea to lock the file before open it
	const BPTR fpsettings = Open((CONST_STRPTR)filename, MODE_OLDFILE);
	if (fpsettings)
	{
		do
		{
			if (FGets(fpsettings, file_line, buffer_size) == NULL)
				break;

			file_line[strlen(file_line) - 1] = '\0';
			if (strlen(file_line) == 0)
				continue;

			if (!strncmp(file_line, "no_guigfx=", 10))
				current_settings->no_guigfx = atoi((const char*)file_line + 10);
			if (!strncmp(file_line, "filter_use_enter=", 17))
				current_settings->filter_use_enter = atoi((const char*)file_line + 17);
			if (!strncmp(file_line, "hide_side_panel=", 16))
				current_settings->hide_side_panel = atoi((const char*)file_line + 16);
			if (!strncmp(file_line, "save_stats_on_exit=", 19))
				current_settings->save_stats_on_exit = atoi((const char*)file_line + 19);
			if (!strncmp(file_line, "no_smart_spaces=", 16))
				current_settings->no_smart_spaces = atoi((const char*)file_line + 16);
			if (!strncmp(file_line, "titles_from_dirs=", 17))
				current_settings->titles_from_dirs = atoi((const char*)file_line + 17);
			if (!strncmp(file_line, "hide_screenshots=", 17))
				current_settings->hide_screenshots = atoi((const char*)file_line + 17);
			if (!strncmp(file_line, "screenshot_width=", 17))
				current_settings->screenshot_width = atoi((const char*)file_line + 17);
			if (!strncmp(file_line, "screenshot_height=", 18))
				current_settings->screenshot_height = atoi((const char*)file_line + 18);
			if (!strncmp(file_line, "start_with_favorites=", 21))
				current_settings->start_with_favorites = atoi((const char*)file_line + 21);
		}
		while (1);

		Close(fpsettings);
	}
	else
	{
		// No "igame.prefs" file found, fallback to reading Tooltypes
		read_tool_types();
	}

	if (file_line)
		free(file_line);

	return current_settings;
}

static void load_games_list(const char* filename)
{
	const int buffer_size = 512;
	STRPTR file_line = malloc(buffer_size * sizeof(char));
	if (file_line == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	const BPTR fpgames = Open((CONST_STRPTR)filename, MODE_OLDFILE);
	if (fpgames)
	{
		if (games != NULL)
		{
			free(games);
			games = NULL;
		}

		do
		{
			if (FGets(fpgames, file_line, buffer_size) == NULL)
				break;

			file_line[strlen(file_line) - 1] = '\0';
			if (strlen(file_line) == 0)
				continue;

			item_games = (games_list *)calloc(1, sizeof(games_list));
			item_games->next = NULL;

			if (!strcmp((char *)file_line, "index=0"))
			{
				item_games->index = 0;
				item_games->exists = 0;
				item_games->deleted = 0;

				do
				{
					if (FGets(fpgames, file_line, 500) == NULL)
						break;

					file_line[strlen(file_line) - 1] = '\0';

					if (strlen(file_line) == 0)
						break;

					//this is to make sure that gameslist goes ok from 1.2 to 1.3
					item_games->hidden = 0;

					if (!strncmp(file_line, "title=", 6))
						strcpy(item_games->title, file_line + 6);
					else if (!strncmp(file_line, "genre=", 6))
						strcpy(item_games->genre, file_line + 6);
					else if (!strncmp(file_line, "path=", 5))
						strcpy(item_games->path, file_line + 5);
					else if (!strncmp(file_line, "favorite=", 9))
						item_games->favorite = atoi((const char*)file_line + 9);
					else if (!strncmp(file_line, "timesplayed=", 12))
						item_games->times_played = atoi((const char*)file_line + 12);
					else if (!strncmp(file_line, "lastplayed=", 11))
						item_games->last_played = atoi((const char*)file_line + 11);
					else if (!strncmp(file_line, "hidden=", 7))
						item_games->hidden = atoi((const char*)file_line + 7);
				}
				while (1);

				if (games == NULL)
				{
					games = item_games;
				}
				else
				{
					item_games->next = games;
					games = item_games;
				}
			}
		}
		while (1); //read of gameslist ends here

		add_games_to_listview();
		Close(fpgames);
	}
	if (file_line)
		free(file_line);
}

static void load_repos(const char* filename)
{
	const int buffer_size = 512;
	STRPTR file_line = malloc(buffer_size * sizeof(char));

	if (file_line == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	const BPTR fprepos = Open((CONST_STRPTR)filename, MODE_OLDFILE);
	if (fprepos)
	{
		while (FGets(fprepos, file_line, buffer_size))
		{
			if (file_line[strlen(file_line) - 1] == '\n') file_line[strlen(file_line) - 1] = '\0';
			if (strlen(file_line) == 0)
				break;

			item_repos = (repos_list *)calloc(1, sizeof(repos_list));
			item_repos->next = NULL;
			strcpy(item_repos->repo, file_line);

			if (repos == NULL)
			{
				repos = item_repos;
			}
			else
			{
				item_repos->next = repos;
				repos = item_repos;
			}

			DoMethod(app->LV_GameRepositories, MUIM_List_InsertSingle, item_repos->repo, 1, MUIV_List_Insert_Bottom);
		}

		Close(fprepos);
	}

	if (file_line)
		free(file_line);
}

static void load_genres(const char* filename)
{
	const int buffer_size = 512;
	STRPTR file_line = malloc(buffer_size * sizeof(char));
	if (file_line == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	int i;
	const BPTR fpgenres = Open((CONST_STRPTR)filename, MODE_OLDFILE);
	if (fpgenres)
	{
		no_of_genres = 0;
		while (FGets(fpgenres, file_line, buffer_size))
		{
			file_line[strlen(file_line) - 1] = '\0';
			if (strlen(file_line) == 0)
				break;

			item_genres = (genres_list *)calloc(1, sizeof(genres_list));
			item_genres->next = NULL;
			strcpy(item_genres->genre, file_line);

			if (genres == NULL)
			{
				genres = item_genres;
			}
			else
			{
				item_genres->next = genres;
				genres = item_genres;
			}

			no_of_genres++;
			DoMethod(app->LV_GenresList, MUIM_List_InsertSingle, item_genres->genre, MUIV_List_Insert_Sorted);
		}

		DoMethod(app->LV_GenresList, MUIM_List_InsertSingle, GetMBString(MSG_UnknownGenre), MUIV_List_Insert_Bottom);

		for (i = 0; i < no_of_genres; i++)
		{
			DoMethod(app->LV_GenresList, MUIM_List_GetEntry, i + 5, &app->CY_PropertiesGenreContent[i]);
		}

		app->CY_PropertiesGenreContent[i] = (unsigned char*)GetMBString(MSG_UnknownGenre);
		app->CY_PropertiesGenreContent[i + 1] = NULL;
		set(app->CY_PropertiesGenre, MUIA_Cycle_Entries, app->CY_PropertiesGenreContent);

		for (i = 0; i < no_of_genres; i++)
		{
			DoMethod(app->LV_GenresList, MUIM_List_GetEntry, i + 5, &app->CY_AddGameGenreContent[i]);
		}

		app->CY_AddGameGenreContent[i] = (unsigned char*)GetMBString(MSG_UnknownGenre);
		app->CY_AddGameGenreContent[i + 1] = NULL;
		set(app->CY_AddGameGenre, MUIA_Cycle_Entries, app->CY_AddGameGenreContent);

		Close(fpgenres);
	}
	if (file_line)
		free(file_line);
}

static void add_default_filters()
{
	DoMethod(app->LV_GenresList, MUIM_List_InsertSingle, GetMBString(MSG_FilterShowAll), MUIV_List_Insert_Bottom);
	DoMethod(app->LV_GenresList, MUIM_List_InsertSingle, GetMBString(MSG_FilterFavorites), MUIV_List_Insert_Bottom);
	DoMethod(app->LV_GenresList, MUIM_List_InsertSingle, GetMBString(MSG_FilterLastPlayed), MUIV_List_Insert_Bottom);
	DoMethod(app->LV_GenresList, MUIM_List_InsertSingle, GetMBString(MSG_FilterMostPlayed), MUIV_List_Insert_Bottom);
	DoMethod(app->LV_GenresList, MUIM_List_InsertSingle, GetMBString(MSG_FilterNeverPlayed), MUIV_List_Insert_Bottom);
}

static void clear_gameslist(void)
{
	// Erase list
	DoMethod(app->LV_GamesList, MUIM_List_Clear);
	set(app->LV_GamesList, MUIA_List_Quiet, TRUE);
}

static void list_show_all(char* str)
{
	char* helper = malloc(200 * sizeof(char));
	if (helper == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	clear_gameslist();
	total_games = 0;

	if (games)
	{
		for (item_games = games; item_games != NULL; item_games = item_games->next)
		{
			if (item_games->deleted != 1)
			{
				strcpy(helper, item_games->title);
				const int length = strlen(helper);
				for (int i = 0; i <= length - 1; i++) helper[i] = tolower(helper[i]);

				if (strstr(helper, (char *)str))
				{
					if (item_games->hidden != 1)
					{
						DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title,
						         MUIV_List_Insert_Sorted);
						total_games++;
					}
				}
			}
		}
	}

	status_show_total();

	if (helper)
		free(helper);
}

static void list_show_favorites(char* str)
{
	char* helper = malloc(200 * sizeof(char));
	if (helper == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	clear_gameslist();
	total_games = 0;

	if (games)
	{
		for (item_games = games; item_games != NULL; item_games = item_games->next)
		{
			strcpy(helper, item_games->title);
			const int length = strlen(helper);
			for (int i = 0; i <= length - 1; i++) helper[i] = tolower(helper[i]);

			if (item_games->favorite == 1 && item_games->hidden != 1)
			{
				if (str == NULL || strstr(helper, (char *)str))
				{
					DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title, MUIV_List_Insert_Sorted);
					total_games++;
				}
			}
		}
	}
	status_show_total();

	if (helper)
		free(helper);
}

static void list_show_last_played(char* str)
{
	char* helper = malloc(200 * sizeof(char));
	if (helper == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	clear_gameslist();
	total_games = 0;

	if (games)
	{
		for (item_games = games; item_games != NULL; item_games = item_games->next)
		{
			if (item_games->deleted != 1)
			{
				strcpy(helper, item_games->title);
				const int length = strlen(helper);
				for (int i = 0; i <= length - 1; i++) helper[i] = tolower(helper[i]);

				if (item_games->last_played == 1 && strstr(helper, (char *)str))
				{
					DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title, MUIV_List_Insert_Sorted);
					total_games++;
				}
			}
		}
	}
	status_show_total();

	if (helper)
		free(helper);
}

static void list_show_most_played(char* str)
{
	char* helper = malloc(200 * sizeof(char));
	if (helper == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	int max = 0;
	clear_gameslist();
	total_games = 0;

	if (games)
	{
		for (item_games = games; item_games != NULL; item_games = item_games->next)
		{
			if (item_games->deleted != 1)
			{
				strcpy(helper, item_games->title);
				const int length = strlen(helper);

				for (int i = 0; i <= length - 1; i++)
					helper[i] = tolower(helper[i]);

				if (item_games->times_played && strstr(helper, (char *)str))
				{
					if (item_games->times_played >= max && item_games->hidden != 1)
					{
						max = item_games->times_played;
						DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title, MUIV_List_Insert_Top);
						total_games++;
					}
					else
					{
						DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title,
						         MUIV_List_Insert_Bottom);
						total_games++;
					}
				}
			}
		}
	}
	status_show_total();

	if (helper)
		free(helper);
}

static void list_show_never_played(char* str)
{
	char* helper = malloc(200 * sizeof(char));
	if (helper == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	clear_gameslist();
	total_games = 0;

	if (games)
	{
		for (item_games = games; item_games != NULL; item_games = item_games->next)
		{
			if (item_games->deleted != 1)
			{
				strcpy(helper, item_games->title);
				const int length = strlen(helper);

				for (int i = 0; i <= length - 1; i++)
					helper[i] = tolower(helper[i]);

				if (item_games->times_played == 0 && item_games->hidden != 1 && strstr(helper, (char *)str))
				{
					DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title, MUIV_List_Insert_Sorted);
					total_games++;
				}
			}
		}
	}
	status_show_total();

	if (helper)
		free(helper);
}

static void list_show_filtered(char* str, char* str_gen)
{
	char* helper = malloc(200 * sizeof(char));
	if (helper == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	clear_gameslist();
	total_games = 0;

	// Find the entries in Games and update the list
	if (games)
	{
		for (item_games = games; item_games != NULL; item_games = item_games->next)
		{
			if (item_games->deleted != 1)
			{
				strcpy(helper, item_games->title);
				const int length = strlen(helper);
				for (int i = 0; i <= length - 1; i++)
					helper[i] = tolower(helper[i]);

				if (!strcmp(item_games->genre, str_gen) && item_games->hidden != 1 && strstr(helper, (char *)str))
				{
					DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title, MUIV_List_Insert_Sorted);
					total_games++;
				}
			}
		}
	}
	status_show_total();

	if (helper)
		free(helper);
}

void app_start(void)
{
	// check if the gamelist csv file exists. If not, try to load the old one
	char csvFilename[32];
	strcpy(csvFilename, (CONST_STRPTR)DEFAULT_GAMESLIST_FILE);
	strcat(csvFilename, ".csv");

	const BPTR gamesListLock = Lock(csvFilename, ACCESS_READ);
	if (gamesListLock) {
		load_games_csv_list(csvFilename);
	} else {
		load_games_list(DEFAULT_GAMESLIST_FILE);
	}
	UnLock(gamesListLock);

	load_repos(DEFAULT_REPOS_FILE);
	add_default_filters();
	load_genres(DEFAULT_GENRES_FILE);
	apply_settings();
	check_for_wbrun();

	IntroPic = 1;

	if (current_settings->start_with_favorites == 1)
	{
		list_show_favorites(NULL);
	}

	DoMethod(app->App,
		MUIM_Application_Load,
		MUIV_Application_Load_ENV
	);

	set(app->WI_MainWindow, MUIA_Window_Open, TRUE);
	set(app->WI_MainWindow, MUIA_Window_ActiveObject, app->LV_GamesList);
}

void filter_change(void)
{
	char* str = NULL;
	char* str_gen = NULL;

	get(app->STR_Filter, MUIA_String_Contents, &str);
	DoMethod(app->LV_GenresList, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &str_gen);

	if (str && strlen(str) != 0)
		for (int i = 0; i <= strlen((char *)str) - 1; i++)
			str[i] = tolower(str[i]);

	if (str_gen == NULL || !strcmp(str_gen, GetMBString(MSG_FilterShowAll)))
		list_show_all(str);

	else if (!strcmp(str_gen, GetMBString(MSG_FilterFavorites)))
		list_show_favorites(str);

	else if (!strcmp(str_gen, GetMBString(MSG_FilterLastPlayed)))
		list_show_last_played(str);

	else if (!strcmp(str_gen, GetMBString(MSG_FilterMostPlayed)))
		list_show_most_played(str);

	else if (!strcmp(str_gen, GetMBString(MSG_FilterNeverPlayed)))
		list_show_never_played(str);

	else
		list_show_filtered(str, str_gen);
}

/*
 * Checks if WBRun exists in C:
 */
static void check_for_wbrun(void)
{
	// TODO: check these to use the check_path_exists() method
	const BPTR oldlock = Lock((CONST_STRPTR)PROGDIR, ACCESS_READ);
	const BPTR lock = Lock("C:WBRun", ACCESS_READ);

	if (lock)
	{
		wbrun=1;
	}
	else
	{
		wbrun=0;
	}

	CurrentDir(oldlock);
}

/*
*   Executes whdload with the slave
*/
void launch_game(void)
{
	struct Library* icon_base;
	struct DiskObject* disk_obj;
	char *game_title = NULL, exec[256], *tool_type;
	int success, i, whdload = 0;
	char str2[512], fullpath[800], helperstr[250], to_check[256];

	// clear vars:
	str2[0] = '\0';
	fullpath[0] = '\0';

	DoMethod(app->LV_GamesList, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &game_title);
	if (game_title == NULL || strlen(game_title) == 0)
	{
		msg_box((const char*)GetMBString(MSG_SelectGameFromList));
		return;
	}

	char* path = malloc(256 * sizeof(char));
	if (path == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}
	get_path(game_title, path);

	if(!check_path_exists(path)) {
		msg_box((const char*)GetMBString(MSG_slavePathDoesntExist));
		return;
	}

	sprintf(helperstr, (const char*)GetMBString(MSG_RunningGameTitle), game_title);
	set(app->TX_Status, MUIA_Text_Contents, helperstr);

	unsigned char* naked_path = malloc(256 * sizeof(char));
	if (naked_path != NULL)
		strip_path(path, (char*)naked_path);
	else
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	char* slave = malloc(256 * sizeof(char));
	if (slave != NULL)
		slave = get_slave_from_path(slave, strlen(naked_path), path);
	else
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}
	string_to_lower(slave);
	if (strstr(slave, ".slave"))
		whdload = 1;

	const BPTR oldlock = Lock((CONST_STRPTR)PROGDIR, ACCESS_READ);
	const BPTR lock = Lock(naked_path, ACCESS_READ);
	if (lock)
		CurrentDir(lock);
	else
	{
		msg_box((const char*)GetMBString(MSG_DirectoryNotFound));
		return;
	}

	if (whdload == 1)
		sprintf(exec, "whdload ");
	else
	{
		if (wbrun)
			sprintf(exec, "C:WBRun \"%s\"", path);
		else
			sprintf(exec, "\"%s\"", path);
	}

	//tooltypes only for whdload games
	if (whdload == 1)
	{
		if (IconBase)
		{
			//scan the .info files in the current dir.
			//one of them should be the game's project icon.

			/*  allocate space for a FileInfoBlock */
#if defined(__amigaos4__)
			struct FileInfoBlock* m = (struct FileInfoBlock *)AllocVecTags(sizeof(struct FileInfoBlock), AVT_ClearWithValue,0, TAG_DONE);
#else
			struct FileInfoBlock* m = (struct FileInfoBlock *)AllocMem(sizeof(struct FileInfoBlock), MEMF_CLEAR);
#endif

			Examine(lock, m);
			if (m->fib_DirEntryType <= 0)
			{
				/*  We don't allow "opta file", only "opta dir" */
				return;
			}

			while ((success = ExNext(lock, m)))
			{
				if (strcasestr(m->fib_FileName, ".info"))
				{
					NameFromLock(lock, (unsigned char*)str2, 512);
					sprintf(fullpath, "%s/%s", str2, m->fib_FileName);

					//lose the .info
					for (i = strlen(fullpath) - 1; i >= 0; i--)
					{
						if (fullpath[i] == '.')
							break;
					}
					fullpath[i] = '\0';

					if ((disk_obj = GetDiskObject((STRPTR)fullpath)))
					{
						if (MatchToolValue(FindToolType(disk_obj->do_ToolTypes, (unsigned char*)"SLAVE"), (STRPTR)slave)
							|| MatchToolValue(FindToolType(disk_obj->do_ToolTypes, (unsigned char*)"slave"), (STRPTR)slave))
						{
							for (char** tool_types = (char **)disk_obj->do_ToolTypes; (tool_type = *tool_types); ++
							     tool_types)
							{
								if (!strncmp(tool_type, "IM", 2)) continue;
								if (tool_type[0] == ' ') continue;
								if (tool_type[0] == '(') continue;
								if (tool_type[0] == '*') continue;
								if (tool_type[0] == ';') continue;
								if (tool_type[0] == '\0') continue;
								if (tool_type[0] == -69) continue; // »
								if (tool_type[0] == -85) continue; // «
								if (tool_type[0] == '.') continue;
								if (tool_type[0] == '=') continue;
								if (tool_type[0] == '#') continue;
								if (tool_type[0] == '!') continue;

								/* Must check here for numerical values */
								/* Those (starting with $ should be transformed to dec from hex) */
								char** temp_tbl = my_split((char *)tool_type, "=");
								if (temp_tbl == NULL
									|| temp_tbl[0] == NULL
									|| !strcmp((char *)temp_tbl[0], " ")
									|| !strcmp((char *)temp_tbl[0], ""))
									continue;

								if (temp_tbl[1] != NULL)
								{
									sprintf(to_check, "%s", temp_tbl[1]);
									if (to_check[0] == '$')
									{
										const int dec_rep = hex2dec(to_check);
										sprintf(tool_type, "%s=%d", temp_tbl[0], dec_rep);
									}
								}
								if (temp_tbl)
									free(temp_tbl);
								//req: more free's

								sprintf(exec, "%s %s", exec, tool_type);
							}

							FreeDiskObject(disk_obj);
							break;
						}
						FreeDiskObject(disk_obj);
					}
				}
			}
		}

		//if we're still here, and exec contains just whdload, add the slave and execute
		if (!strcmp(exec, "whdload "))
		{
			sprintf(exec, "%s %s", exec, path);
		}
	}

	// Cleanup the memory allocations
	if (slave)
		free(slave);
	if (path)
		free(path);
	if (naked_path)
		free(naked_path);

	//set the counters for this game
	for (item_games = games; item_games != NULL; item_games = item_games->next)
	{
		if (!strcmp(item_games->title, game_title))
		{
			item_games->last_played = 1;
			item_games->times_played++;
		}

		if (item_games->last_played == 1 && strcmp(item_games->title, game_title))
		{
			item_games->last_played = 0;
		}
	}

	if (!current_settings->save_stats_on_exit)
		save_list(0);

	success = Execute((unsigned char*)exec, 0, 0);

	if (success == 0)
		msg_box((const char*)GetMBString(MSG_ErrorExecutingWhdload));

	CurrentDir(oldlock);
	status_show_total();
}

/*
* Scans the repos for games
*/
void scan_repositories(void)
{
	char repotemp[256], helperstr[256];

	if (repos)
	{
		for (item_games = games; item_games != NULL; item_games = item_games->next)
		{
			//only apply the not exists hack to slaves that are in the current repos, that will be scanned later
			//Binaries (that are added through add game) should be handled afterwards
			if (strcasestr(item_games->path, ".slave") || strlen(item_games->path) == 0)
				item_games->exists = 0;
			else
				item_games->exists = 1;
		}

		const BPTR currentlock = Lock((CONST_STRPTR)PROGDIR, ACCESS_READ);

		for (item_repos = repos; item_repos != NULL; item_repos = item_repos->next)
		{
			sprintf(repotemp, "%s", item_repos->repo);
			sprintf(helperstr, (const char*)GetMBString(MSG_ScanningPleaseWait), repotemp);
			set(app->TX_Status, MUIA_Text_Contents, helperstr);
			const BPTR oldlock = Lock((unsigned char*)repotemp, ACCESS_READ);

			if (oldlock != 0)
			{
				CurrentDir(oldlock);
				follow_thread(oldlock, 0);
				CurrentDir(currentlock);
			}
			else
			{
				//could not lock
			}
		}

		save_list(1);
		refresh_list(1);
	}
}

static void refresh_sidepanel()
{
	DoMethod(app->GR_sidepanel, MUIM_Group_InitChange);
	DoMethod(app->GR_sidepanel, OM_REMMEMBER, app->IM_GameImage_0);
	DoMethod(app->GR_sidepanel, OM_REMMEMBER, app->Space_Sidepanel);
	DoMethod(app->GR_sidepanel, OM_REMMEMBER, app->LV_GenresList);
	MUI_DisposeObject(app->IM_GameImage_0);
	DoMethod(app->GR_sidepanel, OM_ADDMEMBER, app->IM_GameImage_0 = app->IM_GameImage_1);
	DoMethod(app->GR_sidepanel, OM_ADDMEMBER, app->Space_Sidepanel);
	DoMethod(app->GR_sidepanel, OM_ADDMEMBER, app->LV_GenresList);
	DoMethod(app->GR_sidepanel, MUIM_Group_ExitChange);
}

static void show_screenshot(STRPTR screenshot_path)
{
	static char prvScreenshot[255];

	if (strcmp(screenshot_path, prvScreenshot))
	{
		if (current_settings->no_guigfx)
		{
			app->IM_GameImage_1 = MUI_NewObject(Dtpic_Classname,
				MUIA_Dtpic_Name, screenshot_path,
				MUIA_Frame,      MUIV_Frame_ImageButton,
			End;
		}
		else
		{
			app->IM_GameImage_1 = GuigfxObject,
				MUIA_Guigfx_FileName,     screenshot_path,
				MUIA_Guigfx_Quality,      MUIV_Guigfx_Quality_Best,
				MUIA_Guigfx_ScaleMode,    NISMF_SCALEFREE | NISMF_KEEPASPECT_PICTURE,
				MUIA_Guigfx_Transparency, 0,
				MUIA_Frame,     MUIV_Frame_ImageButton,
				MUIA_FixHeight, current_settings->screenshot_height,
				MUIA_FixWidth,  current_settings->screenshot_width,
			End;
		}

		if (app->IM_GameImage_1)
		{
			refresh_sidepanel();
		}

		strcpy(prvScreenshot, screenshot_path);
	}
}

static char *get_screenshot_path(char *game_title)
{
#if defined(__amigaos4__)
	STRPTR slavePath = AllocVecTags(sizeof(char) * MAX_PATH_SIZE, AVT_ClearWithValue,0, TAG_DONE);
#else
	STRPTR slavePath = AllocVec(sizeof(char) * MAX_PATH_SIZE, MEMF_CLEAR);
#endif
	if(slavePath == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return NULL;
	}
	get_path(game_title, slavePath);

#if defined(__amigaos4__)
	STRPTR gameFolderPath = AllocVecTags(sizeof(char) * MAX_PATH_SIZE, AVT_ClearWithValue,0, TAG_DONE);
#else
	STRPTR gameFolderPath = AllocVec(sizeof(char) * MAX_PATH_SIZE, MEMF_CLEAR);
#endif
	if(gameFolderPath == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return NULL;
	}

	if ((gameFolderPath = getParentPath(slavePath)) != NULL)
	{
#if defined(__amigaos4__)
		STRPTR screenshotPath = AllocVecTags(sizeof(char) * MAX_PATH_SIZE, AVT_ClearWithValue,0, TAG_DONE);
#else
		STRPTR screenshotPath = AllocVec(sizeof(char) * MAX_PATH_SIZE, MEMF_CLEAR);
#endif
		if (screenshotPath == NULL)
		{
			msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
			return NULL;
		}

		// Return the igame.iff from the game folder, if exists
		snprintf(screenshotPath, sizeof(char) * MAX_PATH_SIZE, "%s/igame.iff", gameFolderPath);
		if(checkImageDatatype(screenshotPath))
		{
			return screenshotPath;
		}

		// Return the slave icon from the game folder, if exists
		snprintf(screenshotPath, sizeof(char) * MAX_PATH_SIZE, "%s.info", substring(slavePath, 0, -6));
		if(checkImageDatatype(screenshotPath))
		{
			return screenshotPath;
		}
	}

	// Return the default image from iGame folder, if exists
	if(check_path_exists(DEFAULT_SCREENSHOT_FILE))
	{
		return DEFAULT_SCREENSHOT_FILE;
	}

	return NULL;
}

void game_click(void)
{
	if (current_settings->hide_side_panel || current_settings->hide_screenshots)
		return;

	char *game_title = NULL;
	DoMethod(app->LV_GamesList, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &game_title);

	if (game_title) //for some reason, game_click is called and game_title is null??
	{
#if defined(__amigaos4__)
		STRPTR image_path = AllocVecTags(sizeof(char) * MAX_PATH_SIZE, AVT_ClearWithValue,0, TAG_DONE);
#else
		STRPTR image_path = AllocVec(sizeof(char) * MAX_PATH_SIZE, MEMF_CLEAR);
#endif
		if(image_path == NULL)
		{
			msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
			return;
		}

		strcpy(image_path, get_screenshot_path(game_title));
		if (strcmp(image_path, ""))
		{
			show_screenshot(image_path);
		}
	}
}

// TODO: Seems unused
/* Retrieves the Genre from the file, using the Title */
// int get_genre(char* title, char* genre)
// {
// 	for (item_games = games; item_games != NULL; item_games = item_games->next)
// 	{
// 		if (item_games->deleted != 1)
// 		{
// 			if (!strcmp(title, item_games->title))
// 			{
// 				strcpy(genre, item_games->genre);
// 				break;
// 			}
// 		}
// 	}

// 	return 0;
// }

/*
* Adds a repository (path on the disk)
* to the list of repositories
*/
void repo_add(void)
{
	char* repo_path = NULL;
	get(app->PA_RepoPath, MUIA_String_Contents, &repo_path);

	if (repo_path && strlen(repo_path) != 0)
	{
		item_repos = (repos_list *)calloc(1, sizeof(repos_list));
		item_repos->next = NULL;

		strcpy(item_repos->repo, repo_path);

		if (repos == NULL)
			repos = item_repos;
		else
		{
			item_repos->next = repos;
			repos = item_repos;
		}

		DoMethod(app->LV_GameRepositories, MUIM_List_InsertSingle, item_repos->repo, 1, MUIV_List_Insert_Bottom);
	}
}

void repo_remove(void)
{
	DoMethod(app->LV_GameRepositories, MUIM_List_Remove, MUIV_List_Remove_Active);
}

/*
* Writes the repositories to the repo.prefs file
*/
void repo_stop(void)
{
	const BPTR fprepos = Open((CONST_STRPTR)DEFAULT_REPOS_FILE, MODE_NEWFILE);
	if (!fprepos)
	{
		msg_box((const char*)GetMBString(MSG_CouldNotCreateReposFile));
	}
	else
	{
		CONST_STRPTR repo_path = NULL;
		for (int i = 0;; i++)
		{
			DoMethod(app->LV_GameRepositories, MUIM_List_GetEntry, i, &repo_path);
			if (!repo_path)
				break;

			FPuts(fprepos, repo_path);
			FPutC(fprepos, '\n');
		}
		Close(fprepos);
	}
}

int title_exists(char* game_title)
{
	for (item_games = games; item_games != NULL; item_games = item_games->next)
	{
		if (!strcmp(game_title, item_games->title) && item_games->deleted != 1)
		{
			// Title found
			return 1;
		}
	}
	// Title not found
	return 0;
}

//shows and inits the GameProperties Window
void game_properties(void)
{
	int open = 0;
	//if window is already open
	get(app->WI_Properties, MUIA_Window_Open, &open);
	if (open) return;

	// Allocate Memory for variables
	char* game_title = NULL;
#if defined(__amigaos4__)
	char* helperstr  = AllocVecTags(sizeof(char) * 512, AVT_ClearWithValue,0, TAG_DONE);
	char* fullpath   = AllocVecTags(sizeof(char) * 800, AVT_ClearWithValue,0, TAG_DONE);
	char* str2       = AllocVecTags(sizeof(char) * 512, AVT_ClearWithValue,0, TAG_DONE);
	char* path       = AllocVecTags(sizeof(char) * 256, AVT_ClearWithValue,0, TAG_DONE);
	char* naked_path = AllocVecTags(sizeof(char) * 256, AVT_ClearWithValue,0, TAG_DONE);
	char* slave      = AllocVecTags(sizeof(char) * 256, AVT_ClearWithValue,0, TAG_DONE);
#else
	char* helperstr = AllocMem(512 * sizeof(char), MEMF_CLEAR);
	char* fullpath = AllocMem(800 * sizeof(char), MEMF_CLEAR);
	char* str2 = AllocMem(512 * sizeof(char), MEMF_CLEAR);
	char* path = AllocMem(256 * sizeof(char), MEMF_CLEAR);
	char* naked_path = AllocMem(256 * sizeof(char), MEMF_CLEAR);
	char* slave = AllocMem(256 * sizeof(char), MEMF_CLEAR);
#endif

	// Check if any of them failed
	if (helperstr == NULL
		|| fullpath == NULL
		|| str2 == NULL
		|| path == NULL
		|| naked_path == NULL
		|| slave == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	//set the elements on the window
	DoMethod(app->LV_GamesList, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &game_title);
	if (game_title == NULL || strlen(game_title) == 0)
	{
		msg_box((const char*)GetMBString(MSG_SelectGameFromList));
		return;
	}

	const int found = title_exists(game_title);
	if (!found)
	{
		msg_box((const char*)GetMBString(MSG_SelectGameFromList));
		return;
	}

	int i;
	struct DiskObject* disk_obj;
	char* tool_type;

	set(app->STR_PropertiesGameTitle, MUIA_String_Contents, game_title);
	set(app->TX_PropertiesSlavePath, MUIA_Text_Contents, item_games->path);

	sprintf(helperstr, "%d", item_games->times_played);
	set(app->TX_PropertiesTimesPlayed, MUIA_Text_Contents, helperstr);

	//set the genre
	for (i = 0; i < no_of_genres; i++)
		if (!strcmp(app->CY_PropertiesGenreContent[i], item_games->genre))
			break;
	set(app->CY_PropertiesGenre, MUIA_Cycle_Active, i);

	if (item_games->favorite == 1)
		set(app->CH_PropertiesFavorite, MUIA_Selected, TRUE);
	else
		set(app->CH_PropertiesFavorite, MUIA_Selected, FALSE);

	if (item_games->hidden == 1)
		set(app->CH_PropertiesHidden, MUIA_Selected, TRUE);
	else
		set(app->CH_PropertiesHidden, MUIA_Selected, FALSE);

	//set up the tooltypes
	get_path(game_title, path);
	strip_path(path, naked_path);
	slave = get_slave_from_path(slave, strlen(naked_path), path);
	string_to_lower(slave);

	const BPTR oldlock = Lock((CONST_STRPTR)PROGDIR, ACCESS_READ);
	const BPTR lock = Lock((CONST_STRPTR)naked_path, ACCESS_READ);
	if (lock)
		CurrentDir(lock);
	else
	{
		msg_box((const char*)GetMBString(MSG_DirectoryNotFound));
		return;
	}

	// If it's already been allocated, let's just zero it out
	if (game_tooltypes)
		memset(&game_tooltypes[0], 0, 1024 * sizeof(char));
	else
#if defined(__amigaos4__)
		game_tooltypes = AllocVecTags(sizeof(char *) * 1024, AVT_ClearWithValue,0, TAG_DONE);
#else
		game_tooltypes = AllocMem(1024 * sizeof(char), MEMF_CLEAR);
#endif

	if (game_tooltypes == 0)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	if (IconBase)
	{
		//scan the .info files in the current dir.
		//one of them should be the game's project icon.

		/*  allocate space for a FileInfoBlock */
#if defined(__amigaos4__)
		struct FileInfoBlock* m = (struct FileInfoBlock *)AllocVecTags(sizeof(struct FileInfoBlock), AVT_ClearWithValue,0, TAG_DONE);
#else
		struct FileInfoBlock* m = (struct FileInfoBlock *)AllocMem(sizeof(struct FileInfoBlock), MEMF_CLEAR);
#endif

		int success = Examine(lock, m);
		if (m->fib_DirEntryType <= 0)
		{
			/*  We don't allow "opta file", only "opta dir" */
			return;
		}

		while ((success = ExNext(lock, m)))
		{
			if (strcasestr(m->fib_FileName, ".info"))
			{
				NameFromLock(lock, (unsigned char*)str2, 512);
				sprintf(fullpath, "%s/%s", str2, m->fib_FileName);

				//lose the .info
				for (i = strlen(fullpath) - 1; i >= 0; i--)
				{
					if (fullpath[i] == '.')
						break;
				}
				fullpath[i] = '\0';

				if ((disk_obj = GetDiskObject((STRPTR)fullpath)))
				{
					if (MatchToolValue(FindToolType(disk_obj->do_ToolTypes, (STRPTR)SLAVE_STRING), (STRPTR)slave))
					{
						for (char** tool_types = (char **)disk_obj->do_ToolTypes; (tool_type = *tool_types); ++
						     tool_types)
						{
							if (!strncmp(tool_type, "IM", 2))
								continue;

							sprintf(game_tooltypes, "%s%s\n", game_tooltypes, tool_type);
							set(app->TX_PropertiesTooltypes, MUIA_TextEditor_ReadOnly, FALSE);
						}

						FreeDiskObject(disk_obj);
						break;
					}
					FreeDiskObject(disk_obj);
				}
			}
		}
	}

	// Cleanup the memory allocations
#if defined(__amigaos4__)
	FreeVec(slave);
	FreeVec(path);
	FreeVec(naked_path);
	FreeVec(helperstr);
	FreeVec(fullpath);
	FreeVec(str2);
#else
	if (slave)
		FreeMem(slave, 256 * sizeof(char));
	if (path)
		FreeMem(path, 256 * sizeof(char));
	if (naked_path)
		FreeMem(naked_path, 256 * sizeof(char));
	if (helperstr)
		FreeMem(helperstr, 512 * sizeof(char));
	if (fullpath)
		FreeMem(fullpath, 800 * sizeof(char));
	if (str2)
		FreeMem(str2, 512 * sizeof(char));
#endif

	if (strlen(game_tooltypes) == 0)
	{
		sprintf(game_tooltypes, "No .info file");
		set(app->TX_PropertiesTooltypes, MUIA_TextEditor_ReadOnly, TRUE);
	}
	set(app->TX_PropertiesTooltypes, MUIA_TextEditor_Contents, game_tooltypes);
	CurrentDir(oldlock);
	set(app->WI_Properties, MUIA_Window_Open, TRUE);
}

//when ok is pressed in GameProperties
void game_properties_ok(void)
{
	int fav = 0, genre = 0, hid = 0;
	char* tool_type;
	int i;
	int new_tool_type_count = 1, old_tool_type_count = 0, old_real_tool_type_count = 0;

	char* game_title = NULL;
	char* path = NULL; //AllocMem(256 * sizeof(char), MEMF_CLEAR);
#if defined(__amigaos4__)
	char* fullpath = AllocVecTags(sizeof(char) * 800, AVT_ClearWithValue,0, TAG_DONE);
	char* str2     = AllocVecTags(sizeof(char) * 512, AVT_ClearWithValue,0, TAG_DONE);
#else
	char* fullpath = AllocMem(800 * sizeof(char), MEMF_CLEAR);
	char* str2 = AllocMem(512 * sizeof(char), MEMF_CLEAR);
#endif

	get(app->STR_PropertiesGameTitle, MUIA_String_Contents, &game_title);
	get(app->TX_PropertiesSlavePath, MUIA_Text_Contents, &path);
	get(app->CY_PropertiesGenre, MUIA_Cycle_Active, &genre);
	get(app->CH_PropertiesFavorite, MUIA_Selected, &fav);
	get(app->CH_PropertiesHidden, MUIA_Selected, &hid);

	//find the entry, and update it:
	for (item_games = games; item_games != NULL; item_games = item_games->next)
	{
		if (!strcmp(path, item_games->path))
		{
			if (strcmp(item_games->title, game_title))
			{
				//check dup for title
				if (check_dup_title(game_title))
				{
					msg_box((const char*)GetMBString(MSG_TitleAlreadyExists));
					return;
				}
			}
			strcpy(item_games->title, game_title);
			strcpy(item_games->genre, app->CY_PropertiesGenreContent[genre]);
			if (fav == 1) item_games->favorite = 1;
			else item_games->favorite = 0;

			//if it was previously not hidden, hide now
			if (hid == 1 && item_games->hidden != 1)
			{
				item_games->hidden = 1;
				DoMethod(app->LV_GamesList, MUIM_List_Remove, MUIV_List_Remove_Selected);
				total_games = total_games - 1;
				status_show_total();
			}

			if (hid == 0 && item_games->hidden == 1)
			{
				item_games->hidden = 0;
				DoMethod(app->LV_GamesList, MUIM_List_Remove, MUIV_List_Remove_Selected);
				total_hidden--;
				status_show_total();
			}

			break;
		}
	}

	//update the games tooltypes
	STRPTR tools = (STRPTR)DoMethod(app->TX_PropertiesTooltypes, MUIM_TextEditor_ExportText);

	//tooltypes changed
	if (strcmp((char *)tools, game_tooltypes))
	{
#if defined(__amigaos4__)
		char* naked_path = AllocVecTags(sizeof(char) * 256, AVT_ClearWithValue,0, TAG_DONE);
#else
		char* naked_path = AllocMem(256 * sizeof(char), MEMF_CLEAR);
#endif
		if (naked_path != NULL)
			strip_path(path, naked_path);
		else
		{
			msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
			return;
		}

#if defined(__amigaos4__)
		char* slave = AllocVecTags(sizeof(char) * 256, AVT_ClearWithValue,0, TAG_DONE);
#else
		char* slave = AllocMem(256 * sizeof(char), MEMF_CLEAR);
#endif
		if (slave != NULL)
			get_slave_from_path(slave, strlen(naked_path), path);
		else
		{
			msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
			return;
		}

		string_to_lower(slave);

		const BPTR oldlock = Lock((CONST_STRPTR)PROGDIR, ACCESS_READ);
		const BPTR lock = Lock((CONST_STRPTR)naked_path, ACCESS_READ);
		CurrentDir(lock);

		if (IconBase)
		{
			//scan the .info files in the current dir.
			//one of them should be the game's project icon.

			/*  allocate space for a FileInfoBlock */
#if defined(__amigaos4__)
			struct FileInfoBlock* m = (struct FileInfoBlock *)AllocVecTags(sizeof(struct FileInfoBlock), AVT_ClearWithValue,0, TAG_DONE);
#else
			struct FileInfoBlock* m = (struct FileInfoBlock *)AllocMem(sizeof(struct FileInfoBlock), MEMF_CLEAR);
#endif

			Examine(lock, m);
			if (m->fib_DirEntryType <= 0)
			{
				/*  We don't allow "opta file", only "opta dir" */
				return;
			}

			while (ExNext(lock, m))
			{
				if (strcasestr(m->fib_FileName, ".info"))
				{
					NameFromLock(lock, (unsigned char*)str2, 512);
					sprintf(fullpath, "%s/%s", str2, m->fib_FileName);

					//lose the .info
					for (i = strlen(fullpath) - 1; i >= 0; i--)
					{
						if (fullpath[i] == '.')
							break;
					}
					fullpath[i] = '\0';

					struct DiskObject* disk_obj = GetDiskObject((STRPTR)fullpath);
					if (disk_obj)
					{
						if (MatchToolValue(FindToolType(disk_obj->do_ToolTypes, (STRPTR)SLAVE_STRING), (STRPTR)slave))
						{
							//check numbers for old and new tooltypes
							for (i = 0; i <= strlen(tools); i++)
								if (tools[i] == '\n') new_tool_type_count++;

							//add one for the last tooltype that doesnt end with \n
							new_tool_type_count++;

							for (i = 0; i <= strlen(game_tooltypes); i++)
								if (game_tooltypes[i] == '\n') old_tool_type_count++;

							for (char** tool_types = (char **)disk_obj->do_ToolTypes; (tool_type = *tool_types); ++tool_types)
								old_real_tool_type_count++;

#if defined(__amigaos4__)
							unsigned char** new_tool_types = AllocVecTags(sizeof(char *) * new_tool_type_count, AVT_ClearWithValue,0, TAG_DONE);
#else
							unsigned char** new_tool_types = AllocVec(new_tool_type_count * sizeof(char *),
							                                          MEMF_FAST | MEMF_CLEAR);
#endif
							unsigned char** newptr = new_tool_types;

							char** temp_tbl = my_split((char *)tools, "\n");
							if (temp_tbl == NULL
								|| temp_tbl[0] == NULL
								|| !strcmp((char *)temp_tbl[0], " ")
								|| !strcmp((unsigned char *)temp_tbl[0], ""))
								break;

							for (i = 0; i <= new_tool_type_count - 2; i++)
								*newptr++ = (unsigned char*)temp_tbl[i];

							*newptr = NULL;

							disk_obj->do_ToolTypes = (STRPTR *)new_tool_types;
							PutDiskObject((STRPTR)fullpath, disk_obj);
							FreeDiskObject(disk_obj);

							if (temp_tbl)
								free(temp_tbl);
							if (new_tool_types)
								FreeVec(new_tool_types);

							break;
						}
						FreeDiskObject(disk_obj);
					}
				}
			}
#if defined(__amigaos4__)
			FreeVec(m);
#else
			if (m)
				FreeMem(m, sizeof(struct FileInfoBlock));
#endif
		}

		CurrentDir(oldlock);

		// Cleanup the memory allocations
#if defined(__amigaos4__)
		FreeVec(slave);
		FreeVec(naked_path);
		//FreeVec(game_tooltypes);
		FreeVec(fullpath);
		FreeVec(str2);
#else
		if (slave)
			FreeMem(slave, 256 * sizeof(char));
		if (naked_path)
			FreeMem(naked_path, 256 * sizeof(char));
		//if (game_tooltypes)
		//	FreeMem(game_tooltypes, 1024 * sizeof(char));
		if (fullpath)
			FreeMem(fullpath, 800 * sizeof(char));
		if (str2)
			FreeMem(str2, 512 * sizeof(char));
#endif
	}
	FreeVec(tools);

	DoMethod(app->LV_GamesList, MUIM_List_Redraw, MUIV_List_Redraw_Active);
	set(app->WI_Properties, MUIA_Window_Open, FALSE);
	save_list(0);
}

void list_show_hidden(void)
{
	if (showing_hidden == 0)
	{
		set(app->LV_GamesList, MUIA_List_Quiet, TRUE);
		total_hidden = 0;
		DoMethod(app->LV_GamesList, MUIM_List_Clear);

		set(app->LV_GenresList, MUIA_Disabled, TRUE);
		set(app->STR_Filter, MUIA_Disabled, TRUE);
		if (games)
		{
			/* Find the entries in Games and update the list */
			for (item_games = games; item_games != NULL; item_games = item_games->next)
			{
				if (item_games->hidden == 1 && item_games->deleted != 1)
				{
					DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title, MUIV_List_Insert_Sorted);
					total_hidden++;
				}
			}
		}

		status_show_total();
		showing_hidden = 1;
	}
	else
	{
		set(app->LV_GenresList, MUIA_Disabled, FALSE);
		set(app->STR_Filter, MUIA_Disabled, FALSE);
		showing_hidden = 0;
		refresh_list(0);
	}
}

void app_stop(void)
{
	if (current_settings->save_stats_on_exit)
		save_list(0);

	memset(&fname[0], 0, sizeof fname);

	if (games)
	{
		free(games);
		games = NULL;
	}
	if (repos)
	{
		free(repos);
		repos = NULL;
	}
	if (genres)
	{
		free(genres);
		genres = NULL;
	}
}

void genres_click(void)
{
	filter_change();
}

static void follow_thread(BPTR lock, int tab_level)
{
	int exists = 0, j;
	char str[512], fullpath[512], temptitle[256];

	/*  if at the end of the road, don't print anything */
	if (!lock)
		return;

	/*  allocate space for a FileInfoBlock */
#if defined(__amigaos4__)
		struct FileInfoBlock* m = (struct FileInfoBlock *)AllocVecTags(sizeof(struct FileInfoBlock), AVT_ClearWithValue,0, TAG_DONE);
#else
	struct FileInfoBlock* m = (struct FileInfoBlock *)AllocMem(sizeof(struct FileInfoBlock), MEMF_CLEAR);
#endif

	int success = Examine(lock, m);
	if (m->fib_DirEntryType <= 0)
		return;

	/*  The first call to Examine fills the FileInfoBlock with information
	about the directory.  If it is called at the root level, it contains
	the volume name of the disk.  Thus, this program is only printing
	the output of ExNext rather than both Examine and ExNext.  If it
	printed both, it would list directory entries twice! */

	while ((success = ExNext(lock, m)))
	{
		/*  Print what we've got: */
		if (m->fib_DirEntryType > 0)
		{
		}

		//make m->fib_FileName to lower
		const int kp = strlen((char *)m->fib_FileName);
		for (int s = 0; s < kp; s++) m->fib_FileName[s] = tolower(m->fib_FileName[s]);
		if (strcasestr(m->fib_FileName, ".slave"))
		{
			NameFromLock(lock, (unsigned char*)str, 511);
			sprintf(fullpath, "%s/%s", str, m->fib_FileName);

			/* add the slave to the gameslist (if it does not already exist) */
			for (item_games = games; item_games != NULL; item_games = item_games->next)
			{
				if (!strcmp(item_games->path, fullpath))
				{
					exists = 1;
					item_games->exists = 1;
					break;
				}
			}
			if (exists == 0)
			{
				item_games = (games_list *)calloc(1, sizeof(games_list));
				item_games->next = NULL;

				/* strip the path from the slave file and get the rest */
				for (j = strlen(str) - 1; j >= 0; j--)
				{
					if (str[j] == '/' || str[j] == ':')
						break;
				}

				//CHANGE 2007-12-03: init n=0 here
				int n = 0;
				for (int k = j + 1; k <= strlen(str) - 1; k++)
					temptitle[n++] = str[k];
				temptitle[n] = '\0';

				if (current_settings->titles_from_dirs)
				{
					// If the TITLESFROMDIRS tooltype is enabled, set Titles from Directory names
					const char* title = get_directory_name(fullpath);
					if (title != NULL)
					{
						if (current_settings->no_smart_spaces)
						{
							strcpy(item_games->title, title);
						}
						else
						{
							const char* title_with_spaces = add_spaces_to_string(title);
							strcpy(item_games->title, title_with_spaces);
						}
					}
				}
				else
				{
					// Default behavior: set Titles by the .slave contents
					if (get_title_from_slave(fullpath, item_games->title))
						strcpy(item_games->title, temptitle);
				}

				while (check_dup_title(item_games->title))
				{
					strcat(item_games->title, " Alt");
				}

				strcpy(item_games->genre, GetMBString(MSG_UnknownGenre));
				strcpy(item_games->path, fullpath);
				item_games->favorite = 0;
				item_games->times_played = 0;
				item_games->last_played = 0;
				item_games->exists = 1;
				item_games->hidden = 0;

				if (games == NULL)
				{
					games = item_games;
				}
				else
				{
					item_games->next = games;
					games = item_games;
				}
			}
			exists = 0;
		}

		/*  If we have a directory, then enter it: */
		if (m->fib_DirEntryType > 0)
		{
			/*  Since it is a directory, get a lock on it: */
			const BPTR newlock = Lock((CONST_STRPTR)m->fib_FileName, ACCESS_READ);

			/*  cd to this directory: */
			const BPTR oldlock = CurrentDir(newlock);

			/*  recursively follow the thread down to the bottom: */
			follow_thread(newlock, tab_level + 1);

			/*  cd back to where we used to be: */
			CurrentDir(oldlock);

			/*  Unlock the directory we just visited: */
			if (newlock)
				UnLock(newlock);
		}
	}
#if defined(__amigaos4__)
	FreeVec(m);
#else
	FreeMem(m, sizeof(struct FileInfoBlock));
#endif
}

static void refresh_list(const int check_exists)
{
	DoMethod(app->LV_GamesList, MUIM_List_Clear);
	total_games = 0;
	set(app->LV_GamesList, MUIA_List_Quiet, TRUE);

	if (check_exists == 0)
	{
		for (item_games = games; item_games != NULL; item_games = item_games->next)
		{
			if (strlen(item_games->path) != 0
				&& item_games->hidden != 1
				&& item_games->deleted != 1)
			{
				total_games++;
				DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title, MUIV_List_Insert_Sorted);
			}
		}
	}
	else
	{
		for (item_games = games; item_games != NULL; item_games = item_games->next)
		{
			if (strlen(item_games->path) != 0
				&& item_games->hidden != 1
				&& item_games->exists == 1
				&& item_games->deleted != 1)
			{
				total_games++;
				DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title, MUIV_List_Insert_Sorted);
			}
			// else
			// {
			// 	printf("[%s] [%s] [%d]\n", item_games->title, item_games->path, item_games->exists);
			// }
		}
	}

	status_show_total();
}

void save_list(const int check_exists)
{
	save_to_csv(DEFAULT_GAMESLIST_FILE, check_exists);
}

void save_list_as(void)
{
	//TODO: Check if file exists, warn the user about overwriting it
	if (get_filename("Save List As...", "Save", TRUE))
		save_to_csv(fname, 0);
}

void open_list(void)
{
	if (get_filename("Open List", "Open", FALSE))
	{
		clear_gameslist();
		load_games_list(fname);
	}
}

//function to return the dec eq of a hex no.
static int hex2dec(char* hexin)
{
	int dec;
	//lose the first $ character
	hexin++;
	sscanf(hexin, "%x", &dec);
	return dec;
}

void game_duplicate(void)
{
	char* str = NULL;
	DoMethod(app->LV_GamesList, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &str);

	if (str == NULL || strlen(str) == 0)
	{
		msg_box((const char*)GetMBString(MSG_SelectGameFromList));
		return;
	}

	int found = 0;
	for (item_games = games; item_games != NULL; item_games = item_games->next)
	{
		if (!strcmp(str, item_games->title) && item_games->deleted != 1)
		{
			found = 1;
			break;
		}
	}

	if (!found)
	{
		msg_box((const char*)GetMBString(MSG_SelectGameFromList));
		return;
	}

	char title_copy[200];
	char path_copy[256];
	char genre_copy[100];
	strcpy(title_copy, item_games->title);
	strcat(title_copy, " copy");
	strcpy(path_copy, item_games->path);
	strcpy(genre_copy, item_games->genre);

	item_games = (games_list *)calloc(1, sizeof(games_list));
	item_games->next = NULL;
	item_games->index = 0;
	item_games->exists = 0;
	item_games->deleted = 0;
	strcpy(item_games->title, title_copy);
	strcpy(item_games->genre, genre_copy);
	strcpy(item_games->path, path_copy);
	item_games->favorite = 0;
	item_games->times_played = 0;
	item_games->last_played = 0;
	item_games->hidden = 0;

	if (games == NULL)
		games = item_games;
	else
	{
		item_games->next = games;
		games = item_games;
	}

	total_games++;
	DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title, MUIV_List_Insert_Sorted);
	status_show_total();
}

void game_delete(void)
{
	char* str = NULL;
	LONG id = MUIV_List_NextSelected_Start;
	for (;;)
	{
		DoMethod(app->LV_GamesList, MUIM_List_NextSelected, &id);
		if (id == MUIV_List_NextSelected_End) break;

		DoMethod(app->LV_GamesList, MUIM_List_GetEntry, id, &str);
		for (item_games = games; item_games != NULL; item_games = item_games->next)
		{
			if (!strcmp(str, item_games->title))
			{
				item_games->deleted = 1;
				break;
			}
		}
		DoMethod(app->LV_GamesList, MUIM_List_Remove, id);
		total_games--;
	}
	status_show_total();
}

static LONG xget(Object* obj, ULONG attribute)
{
	LONG x;
	get(obj, attribute, &x);
	return x;
}

char* get_str(Object* obj)
{
	return (char *)xget(obj, MUIA_String_Contents);
}

// TODO: Seems unused
// BOOL get_bool(Object* obj)
// {
// 	return (BOOL)xget(obj, MUIA_Selected);
// }

static int get_cycle_index(Object* obj)
{
	int index = 0;
	get(obj, MUIA_Cycle_Active, &index);
	return index;
}

static int get_radio_index(Object* obj)
{
	int index = 0;
	get(obj, MUIA_Radio_Active, &index);
	return index;
}

void setting_filter_use_enter_changed(void)
{
	current_settings->filter_use_enter = (BOOL)xget(app->CH_FilterUseEnter, MUIA_Selected);
}

void setting_save_stats_on_exit_changed(void)
{
	current_settings->save_stats_on_exit = (BOOL)xget(app->CH_SaveStatsOnExit, MUIA_Selected);
}

void setting_smart_spaces_changed(void)
{
	current_settings->no_smart_spaces = (BOOL)xget(app->CH_SmartSpaces, MUIA_Selected);
}

void setting_titles_from_changed(void)
{
	const int index = get_radio_index(app->RA_TitlesFrom);

	// Index=0 -> Titles from Slaves
	// Index=1 -> Titles from Dirs
	current_settings->titles_from_dirs = index;

	if (index == 1)
		set(app->CH_SmartSpaces, MUIA_Disabled, FALSE);
	else
		set(app->CH_SmartSpaces, MUIA_Disabled, TRUE);
}

void setting_hide_screenshot_changed(void)
{
	current_settings->hide_screenshots = (BOOL)xget(app->CH_Screenshots, MUIA_Selected);
}

void setting_no_guigfx_changed(void)
{
	current_settings->no_guigfx = (BOOL)xget(app->CH_NoGuiGfx, MUIA_Selected);
}

void setting_screenshot_size_changed(void)
{
	const int index = get_cycle_index(app->CY_ScreenshotSize);

	// Index=0 -> 160x128
	// Index=1 -> 320x256
	// Index=2 -> Custom size

	if (index == 0)
	{
		set(app->STR_Width, MUIA_Disabled, TRUE);
		set(app->STR_Height, MUIA_Disabled, TRUE);
		current_settings->screenshot_width = 160;
		current_settings->screenshot_height = 128;
		return;
	}

	if (index == 1)
	{
		set(app->STR_Width, MUIA_Disabled, TRUE);
		set(app->STR_Height, MUIA_Disabled, TRUE);
		current_settings->screenshot_width = 320;
		current_settings->screenshot_height = 256;
		return;
	}

	if (index == 2)
	{
		set(app->STR_Width, MUIA_Disabled, FALSE);
		set(app->STR_Height, MUIA_Disabled, FALSE);
	}
}

void settings_use(void)
{
	if (current_settings == NULL)
		return;

	const int index = get_cycle_index(app->CY_ScreenshotSize);
	if (index == 2)
	{
		current_settings->screenshot_width = (int)get_str(app->STR_Width);
		current_settings->screenshot_height = (int)get_str(app->STR_Height);
	}

	set(app->WI_Settings, MUIA_Window_Open, FALSE);
}

void settings_save(void)
{
	settings_use();

	const int buffer_size = 512;
	char* file_line = malloc(buffer_size * sizeof(char));
	if (file_line == NULL)
	{
		msg_box((const char*)GetMBString(MSG_NotEnoughMemory));
		return;
	}

	const BPTR fpsettings = Open((CONST_STRPTR)DEFAULT_SETTINGS_FILE, MODE_NEWFILE);
	if (!fpsettings)
	{
		msg_box((const char*)"Could not save Settings file!");
		return;
	}

	snprintf(file_line, buffer_size, "no_guigfx=%d\n", current_settings->no_guigfx);
	FPuts(fpsettings, (CONST_STRPTR)file_line);
	snprintf(file_line, buffer_size, "filter_use_enter=%d\n", current_settings->filter_use_enter);
	FPuts(fpsettings, (CONST_STRPTR)file_line);
	snprintf(file_line, buffer_size, "hide_side_panel=%d\n", current_settings->hide_side_panel);
	FPuts(fpsettings, (CONST_STRPTR)file_line);
	snprintf(file_line, buffer_size, "start_with_favorites=%d\n", current_settings->start_with_favorites);
	FPuts(fpsettings, (CONST_STRPTR)file_line);
	snprintf(file_line, buffer_size, "save_stats_on_exit=%d\n", current_settings->save_stats_on_exit);
	FPuts(fpsettings, (CONST_STRPTR)file_line);
	snprintf(file_line, buffer_size, "no_smart_spaces=%d\n", current_settings->no_smart_spaces);
	FPuts(fpsettings, (CONST_STRPTR)file_line);
	snprintf(file_line, buffer_size, "titles_from_dirs=%d\n", current_settings->titles_from_dirs);
	FPuts(fpsettings, (CONST_STRPTR)file_line);
	snprintf(file_line, buffer_size, "hide_screenshots=%d\n", current_settings->hide_screenshots);
	FPuts(fpsettings, (CONST_STRPTR)file_line);
	snprintf(file_line, buffer_size, "screenshot_width=%d\n", current_settings->screenshot_width);
	FPuts(fpsettings, (CONST_STRPTR)file_line);
	snprintf(file_line, buffer_size, "screenshot_height=%d\n", current_settings->screenshot_height);
	FPuts(fpsettings, (CONST_STRPTR)file_line);

	Close(fpsettings);
	if (file_line)
		free(file_line);
}

void setting_hide_side_panel_changed(void)
{
	current_settings->hide_side_panel = (BOOL)xget(app->CH_HideSidepanel, MUIA_Selected);
}

void setting_start_with_favorites_changed(void)
{
	current_settings->start_with_favorites = (BOOL)xget(app->CH_StartWithFavorites, MUIA_Selected);
}

void msg_box(const char* msg)
{
	msgbox.es_StructSize = sizeof msgbox;
	msgbox.es_Flags = 0;
	msgbox.es_Title = (UBYTE*)GetMBString(MSG_WI_MainWindow);
	msgbox.es_TextFormat = (unsigned char*)msg;
	msgbox.es_GadgetFormat = (UBYTE*)GetMBString(MSG_BT_AboutOK);

	EasyRequest(NULL, &msgbox, NULL);
}

void get_screen_size(int *width, int *height)
{
	struct Screen* wbscreen;

	if (IntuitionBase)
	{
		if (GfxBase)
		{
			if ((wbscreen = LockPubScreen((CONST_STRPTR)WB_PUBSCREEN_NAME)))
			{
				/* Using intuition.library/GetScreenDrawInfo(), we get the pen
				* array we'll use for the screen clone the easy way. */
				struct DrawInfo* drawinfo = GetScreenDrawInfo(wbscreen);

				struct ViewPort* vp = &wbscreen->ViewPort;
				/* Use graphics.library/GetVPModeID() to get the ModeID of the
				* Workbench screen. */
				if (GetVPModeID(vp) != (long)INVALID_ID)
				{
					*width = wbscreen->Width;
					*height = wbscreen->Height;
				}
				else
				{
					*width = -1;
					*height = -1;
				}

				FreeScreenDrawInfo(wbscreen, drawinfo);
				UnlockPubScreen(NULL, wbscreen);
			}
			else
			{
				*width = -1;
				*height = -1;
			}
		}
	}
}

void add_non_whdload(void)
{
	set(app->STR_AddTitle, MUIA_String_Contents, NULL);
	set(app->PA_AddGame, MUIA_String_Contents, NULL);
	set(app->WI_AddNonWHDLoad, MUIA_Window_Open, TRUE);
}

void non_whdload_ok(void)
{
	char *str, *str_title;
	int genre = 0;

	get(app->PA_AddGame, MUIA_String_Contents, &str);
	get(app->STR_AddTitle, MUIA_String_Contents, &str_title);
	get(app->CY_AddGameGenre, MUIA_Cycle_Active, &genre);

	if (strlen(str_title) == 0)
	{
		msg_box((const char*)GetMBString(MSG_NoTitleSpecified));
		return;
	}
	if (strlen(str) == 0)
	{
		msg_box((const char*)GetMBString(MSG_NoExecutableSpecified));
		return;
	}

	//add the game to the list
	item_games = (games_list *)calloc(1, sizeof(games_list));
	item_games->next = NULL;
	item_games->index = 0;
	strcpy(item_games->title, (char *)str_title);
	strcpy(item_games->genre, app->CY_AddGameGenreContent[genre]);
	strcpy(item_games->path, (char *)str);
	item_games->favorite = 0;
	item_games->times_played = 0;
	item_games->last_played = 0;

	if (games == NULL)
	{
		games = item_games;
	}
	else
	{
		item_games->next = games;
		games = item_games;
	}

	//todo: Small bug. If the list is showing another genre, do not insert it.
	DoMethod(app->LV_GamesList, MUIM_List_InsertSingle, item_games->title, MUIV_List_Insert_Sorted);
	total_games++;
	status_show_total();

	save_list(0);

	set(app->WI_AddNonWHDLoad, MUIA_Window_Open, FALSE);
}

/*
* Checks if the title already exists
* returns 1 if yes, 0 otherwise
*/
static int check_dup_title(char* title)
{
	for (games_list* check_games = games; check_games != NULL; check_games = check_games->next)
	{
		if (!strcmp(check_games->title, title))
		{
			return 1;
		}
	}
	return 0;
}

static void joy_left(void)
{
  char *curr_game = NULL, *prev_game = NULL, *last_game = NULL;
  int ind;

  DoMethod(app->LV_GamesList, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &curr_game);
  get(app->LV_GamesList, MUIA_List_Active, &ind);

  if (curr_game == NULL || strlen(curr_game) == 0)
    {
      set(app->LV_GamesList, MUIA_List_Active, MUIV_List_Active_Top);
      return;
    }

  DoMethod(app->LV_GamesList, MUIM_List_GetEntry, ind--, &prev_game);
  if (prev_game == NULL)
	return;

  while (toupper(curr_game[0]) == toupper(prev_game[0]))
    {
      DoMethod(app->LV_GamesList, MUIM_List_GetEntry, ind--, &prev_game);
      if (prev_game == NULL)
	return;
    }

  last_game = prev_game;

  while (toupper(last_game[0]) == toupper(prev_game[0]))
    {
      DoMethod(app->LV_GamesList, MUIM_List_GetEntry, ind--, &prev_game);
      if (prev_game == NULL)
	return;
    }

  set(app->LV_GamesList, MUIA_List_Active, ind+2);
}

static void joy_right(void)
{
  char *curr_game = NULL, *next_game = NULL;
  int ind;

  DoMethod(app->LV_GamesList, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &curr_game);
  get(app->LV_GamesList, MUIA_List_Active, &ind);

  if (curr_game == NULL || strlen(curr_game) == 0)
    {
      set(app->LV_GamesList, MUIA_List_Active, MUIV_List_Active_Top);
      return;
    }

  DoMethod(app->LV_GamesList, MUIM_List_GetEntry, ind++, &next_game);
  if (next_game == NULL)
	return;

  while (toupper(curr_game[0]) == toupper(next_game[0]))
    {
      DoMethod(app->LV_GamesList, MUIM_List_GetEntry, ind++, &next_game);
      if (next_game == NULL)
	return;
    }

  set(app->LV_GamesList, MUIA_List_Active, ind-1);

}

ULONG get_wb_version(void)
{
	static ULONG ver = 0UL;

	if (ver != 0UL)
	{
		return ver;
	}

	if (WorkbenchBase == NULL)
	{
		//Really Workbench library should be already opened
		// Somehow we're running without Workbench.
		// Nothing to do since the version variable inits with 0.
		return ver;
	}

	// Save workbench.library version for later calls
	ver = WorkbenchBase->lib_Version;

	return ver;
}


static void joystick_buttons(ULONG val)
{
	//if (val & JPF_BUTTON_PLAY) printf("[PLAY/MMB]\n");
	//if (val & JPF_BUTTON_REVERSE) printf("[REVERSE]\n");
	//if (val & JPF_BUTTON_FORWARD) printf("[FORWARD]\n");
	//if (val & JPF_BUTTON_GREEN) printf("[SHUFFLE]\n");
	if (val & JPF_BUTTON_RED)
	{
		launch_game();
	}
	//if (val & JPF_BUTTON_BLUE) printf("[STOP/RMB]\n");
}

static void joystick_directions(ULONG val)
{
	if (val & JPF_JOY_UP)
		set(app->LV_GamesList, MUIA_List_Active, MUIV_List_Active_Up);

	if (val & JPF_JOY_DOWN)
		set(app->LV_GamesList, MUIA_List_Active, MUIV_List_Active_Down);

	if (val & JPF_JOY_LEFT)
	  joy_left();

	if (val & JPF_JOY_RIGHT)
	  joy_right();
}

void joystick_input(ULONG val)
{
	if ((val & JP_TYPE_MASK) == JP_TYPE_NOTAVAIL)
		return;
	if ((val & JP_TYPE_MASK) == JP_TYPE_UNKNOWN)
		return;
	if ((val & JP_TYPE_MASK) == JP_TYPE_MOUSE)
		return;

	if ((val & JP_TYPE_MASK) == JP_TYPE_JOYSTK)
	{
		joystick_directions(val);
		joystick_buttons(val);
	}

	if ((val & JP_TYPE_MASK) == JP_TYPE_GAMECTLR)
	{
		joystick_directions(val);
		joystick_buttons(val);
	}
}
