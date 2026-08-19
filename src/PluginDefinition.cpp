//this file is part of notepad++
//Copyright (C)2003 Don HO <donho@altern.org>
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "PluginDefinition.h"
#include "menuCmdID.h"
#include "assert.h"

#include <list>
#include <string>
#include <regex>
#include <cstddef>
#include "TaskListDlg.h"
#include "AboutDialog\AboutDlg.h"
#include "config.h"

// global values
HINSTANCE	g_hInstance = NULL;
NppData		g_NppData;

TaskListDlg _taskList;
AboutDialog _aboutDlg;

UINT_PTR OUTBOUND_TIMER_ID = 98712323;

//
// The plugin data that Notepad++ needs
//
FuncItem funcItem[nbFunc];

//
// The data of Notepad++ that you can use in your plugin commands
//
NppData nppData;

constexpr auto DOCKABLE_DEMO_INDEX = 15;

void reload_config_file()
{
	e_config_load_result load_result= load_config_file();

	if (load_result==_config_load_default_failed)
	{
		MessageBox(NULL, TEXT("Failed to load default config, Task List will not work"), TEXT("NPP Task List"), MB_OK);
	}
	else if (load_result==_config_load_file_failed)
	{
		MessageBox(NULL, TEXT("Failed loading config file, falling back to defaults (only 'TODO:' is supported)"), TEXT("NPP Task List"), MB_OK);
	}

	findTasks();
}

//
// Initialize your plugin data here
// It will be called while plugin loading   
void pluginInit(HINSTANCE hModule)
{
	g_hInstance = hModule;

	// Initialize dockable demo dialog
	_taskList.init(hModule, NULL);
	reload_config_file();
	
}

//
// Here you can do the clean up, save the parameters (if any) for the next session
//
void pluginCleanUp()
{
	KillTimer(nppData._nppHandle, OUTBOUND_TIMER_ID);
	unload_config_file();

}

//
// Initialization of your plugin commands
// You should fill your plugins commands here
void commandMenuInit(NppData aNppData)
{
	g_NppData = aNppData;

	_aboutDlg.init(g_hInstance, g_NppData);

    //--------------------------------------------//
    //-- STEP 3. CUSTOMIZE YOUR PLUGIN COMMANDS --//
    //--------------------------------------------//
    // with function :
    // setCommand(int index,                      // zero based number to indicate the order of command
    //            TCHAR *commandName,             // the command name that you want to see in plugin menu
    //            PFUNCPLUGINCMD functionPointer, // the symbol of function (function pointer) associated with this command. The body should be defined below. See Step 4.
    //            ShortcutKey *shortcut,          // optional. Define a shortcut to trigger this command
    //            bool check0nInit                // optional. Make this menu item be checked visually
    //            );
	setCommand(0, TEXT("Show Tag List"), &displayDialog, NULL, false);
	setCommand(1, TEXT("Reload Tag List Configuration"), &reload_config_file, NULL, false);
	setCommand(2, TEXT("About Tag List"), &displayAboutDialog, NULL, false);
	setCommand(3, TEXT("Edit Tag List Configuration"), &editConfigFile, NULL, false);
	displayDialog();
}

//
// Here you can do the clean up (especially for the shortcut)
//
void commandMenuCleanUp()
{
	// Don't forget to deallocate your shortcut here
}


//
// This function help you to initialize your plugin commands
//
bool setCommand(size_t index, TCHAR *cmdName, PFUNCPLUGINCMD pFunc, ShortcutKey *sk, bool check0nInit) 
{
    if (index >= nbFunc)
        return false;

    if (!pFunc)
        return false;

    lstrcpy(funcItem[index]._itemName, cmdName);
    funcItem[index]._pFunc = pFunc;
    funcItem[index]._init2Check = check0nInit;
    funcItem[index]._pShKey = sk;

    return true;
}

//----------------------------------------------//
//-- STEP 4. DEFINE YOUR ASSOCIATED FUNCTIONS --//
//----------------------------------------------//


bool needRescanTodos = false;

//find all tasks
void findTasks()
{
	needRescanTodos = true;

}





VOID CALLBACK MyTimerProc(
	HWND /*hwnd*/,        // handle to window for timer messages 
	UINT /*message*/,     // WM_TIMER message 
	UINT /*idTimer*/,     // timer identifier 
	DWORD /*dwTime*/)     // current system time 
{
	if (!_taskList.isCreated())
		return;


	//do not scan document more frequently than once in half a second
	if (!needRescanTodos) {
		return;
	}
	needRescanTodos = false;

    // Open a new document
    //::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_NEW);

    // Get the current scintilla
    int which = -1;
    ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    if (which == -1)
        return;
    HWND curScintilla = (which == 0)?nppData._scintillaMainHandle:nppData._scintillaSecondHandle;

	//list of todo items
	std::list<TodoItem> todos;

	//get length SCI_GETLENGTH
	LRESULT length = ::SendMessage(curScintilla, SCI_GETLENGTH, 0, 0);
	//search for todos: (starting at character 0) SCI_FINDTEXT

	int keyword_count;
	const char * const *keywords= get_keyword_list(&keyword_count);

	// Read entire document into a string for regex scanning
	Sci_TextRange fullRange{};
	fullRange.chrg.cpMin = 0;
	fullRange.chrg.cpMax = static_cast<Sci_PositionCR>(length);
	fullRange.lpstrText = new char[length + 1];
	::SendMessage(curScintilla, SCI_GETTEXTRANGE, 0, (LPARAM)&fullRange);
	std::string docText(fullRange.lpstrText, static_cast<size_t>(length));
	delete[] fullRange.lpstrText;

	// All keywords are treated as regular expressions. No automatic escaping or "re:/.../" markers.

	for (int keyword_index= 0; keyword_index<keyword_count; keyword_index++)
	{
		const char *keyword_c = keywords[keyword_index];
		assert(strlen(keyword_c) < k_max_keyword_length);
		std::string keyword(keyword_c);

		// Treat keyword directly as a regular expression pattern
		std::string pattern = keyword;

		try {
			std::regex re(pattern);

			// perform per-line matching so ^/$ match line boundaries
			size_t docPos = 0;
			const size_t docSize = docText.size();
			while (docPos < docSize) {
				size_t newlinePos = docText.find('\n', docPos);
				size_t lineLen = (newlinePos == std::string::npos) ? (docSize - docPos) : (newlinePos - docPos);
				// handle optional CR before LF (\r\n)
				if (lineLen > 0 && docText[docPos + lineLen - 1] == '\r') {
					--lineLen;
				}

				std::string line = docText.substr(docPos, lineLen);

				for (std::sregex_iterator it(line.begin(), line.end(), re), end; it != end; ++it) {
					std::smatch m = *it;
					size_t matchPos = static_cast<size_t>(m.position());
					size_t lenm = static_cast<size_t>(m.length());
					size_t globalPos = docPos + matchPos;

					TodoItem item{};
					item.hScintilla = curScintilla;

					if (m.size() > 1) {
						// There are capture groups. Show only the concatenation of group matches.
						// Compute display text and compute start/end as span from first group's start to last group's end.
						std::string display;
						size_t firstGlobal = SIZE_MAX;
						size_t lastGlobalEnd = 0;

						for (size_t gi = 1; gi < m.size(); ++gi) {
							std::ssub_match g = m[gi];
							if (!g.matched)
								continue;
							size_t gpos = static_cast<size_t>(g.first - line.begin());
							size_t glen = static_cast<size_t>(g.second - g.first);
							size_t gGlobalPos = docPos + gpos;
							if (firstGlobal == SIZE_MAX) firstGlobal = gGlobalPos;
							if (gGlobalPos + glen > lastGlobalEnd) lastGlobalEnd = gGlobalPos + glen;

							if (!display.empty())
								display.push_back(' ');
							display.append(g.first, g.second);
						}

						if (firstGlobal == SIZE_MAX) {
							// No groups matched (shouldn't happen), fallback to full match
							item.startPosition = static_cast<Sci_PositionCR>(globalPos);
							item.endPosition = static_cast<Sci_PositionCR>(globalPos + lenm);
							item.text = new char[lenm + 1];
							memcpy(item.text, docText.data() + globalPos, lenm);
							item.text[lenm] = '\0';
						} else {
							item.startPosition = static_cast<Sci_PositionCR>(firstGlobal);
							item.endPosition = static_cast<Sci_PositionCR>(lastGlobalEnd);
							// allocate and copy display text
							item.text = new char[display.size() + 1];
							memcpy(item.text, display.data(), display.size());
							item.text[display.size()] = '\0';
						}
					} else {
						// No capture groups: use whole match
						item.startPosition = static_cast<Sci_PositionCR>(globalPos);
						item.endPosition = static_cast<Sci_PositionCR>(globalPos + lenm);
						// allocate and copy matched text
						item.text = new char[lenm + 1];
						memcpy(item.text, docText.data() + globalPos, lenm);
						item.text[lenm] = '\0';
					}

					todos.push_back(item);
				}

				if (newlinePos == std::string::npos)
					break;
				docPos = newlinePos + 1;
			}
		}
		catch (const std::regex_error &) {
			// invalid regex: skip this keyword
			continue;
		}
	}
	//display all todo's
	if (_taskList.itemsFingerprint(todos) != _taskList.todoItemsFingerprint) {
		_taskList.SetList(todos);
	}
	
	//cleanup list
	for (const auto &it : todos)
	{
		delete[] it.text;
	}
	todos.clear();
}


bool timerSettedUp = false;
UINT_PTR uResult;


void displayDialog()
{
	//open pane
	OpenTaskListDockableDlg();

	if (!timerSettedUp){

		timerSettedUp = true;
		uResult = SetTimer(nppData._nppHandle,      // handle to main window 
			OUTBOUND_TIMER_ID,
			200,
			(TIMERPROC)MyTimerProc);
	}

	findTasks();
}


void displayAboutDialog()
{
	_aboutDlg.doDialog();
}

// Open configuration file inside the current Notepad++ instance
void editConfigFile()
{
	// Path must be a wide string. Use the same relative path as in config.cpp
	const wchar_t *configPath = L"./plugins/NppTaskList/config/npp_task_list.cfg";
	::SendMessage(nppData._nppHandle, NPPM_DOOPEN, 0, (LPARAM)configPath);
}


// Dockable Dialog Demo
// 
// This demonstration shows you how to do a dockable dialog.
// You can create your own non dockable dialog - in this case you don't nedd this demonstration.
// You have to create your dialog by inherented DockingDlgInterface class in order to make your dialog dockable
// - please see DemoDlg.h and DemoDlg.cpp to have more informations.
void OpenTaskListDockableDlg()
{
	_taskList.setParent(nppData._nppHandle);
	tTbData	data = {0};

	if (!_taskList.isCreated())
	{
		_taskList.create(&data);

		// define the default docking behaviour
		data.uMask = DWS_DF_CONT_RIGHT;

		data.pszModuleName = _taskList.getPluginFileName();

		// the dlgDlg should be the index of funcItem where the current function pointer is
		// in this case is DOCKABLE_DEMO_INDEX
		data.dlgID = DOCKABLE_DEMO_INDEX;
		::SendMessage(nppData._nppHandle, NPPM_DMMREGASDCKDLG, 0, (LPARAM)&data);
	}
	_taskList.display();
}