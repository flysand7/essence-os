// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#pragma GCC diagnostic ignored "-Wunused-variable" push

#define DEFINE_INTERFACE_STRING(name, text) static const char *interfaceString_ ## name = text;
#define INTERFACE_STRING(name) interfaceString_ ## name, -1

#define ELLIPSIS "\u2026"
#define HYPHENATION_POINT "\u2027"
#define OPEN_SPEECH "\u201C"
#define CLOSE_SPEECH "\u201D"

#define SYSTEM_BRAND_SHORT "Essence"

// Common.

DEFINE_INTERFACE_STRING(CommonErrorTitle, "Error");

DEFINE_INTERFACE_STRING(CommonOK, "OK");
DEFINE_INTERFACE_STRING(CommonCancel, "Cancel");

DEFINE_INTERFACE_STRING(CommonUndo, "Undo");
DEFINE_INTERFACE_STRING(CommonRedo, "Redo");
DEFINE_INTERFACE_STRING(CommonClipboardCut, "Cut");
DEFINE_INTERFACE_STRING(CommonClipboardCopy, "Copy");
DEFINE_INTERFACE_STRING(CommonClipboardPaste, "Paste");
DEFINE_INTERFACE_STRING(CommonSelectionSelectAll, "Select all");
DEFINE_INTERFACE_STRING(CommonSelectionDelete, "Delete");

DEFINE_INTERFACE_STRING(CommonFormatPopup, "Format");
DEFINE_INTERFACE_STRING(CommonFormatSize, "Text size:");
DEFINE_INTERFACE_STRING(CommonFormatLanguage, "Language:");
DEFINE_INTERFACE_STRING(CommonFormatPlainText, "Plain text");

DEFINE_INTERFACE_STRING(CommonFileMenu, "File");
DEFINE_INTERFACE_STRING(CommonFileSave, "Save");
DEFINE_INTERFACE_STRING(CommonFileShare, "Share");
DEFINE_INTERFACE_STRING(CommonFileMakeCopy, "Make a copy");
DEFINE_INTERFACE_STRING(CommonFileVersionHistory, "Version history" ELLIPSIS);
DEFINE_INTERFACE_STRING(CommonFileShowInFileManager, "Show in File Manager" ELLIPSIS);
DEFINE_INTERFACE_STRING(CommonFileMenuFileSize, "Size:");
DEFINE_INTERFACE_STRING(CommonFileMenuFileLocation, "Where:");
DEFINE_INTERFACE_STRING(CommonFileUnchanged, "(All changes saved.)");

DEFINE_INTERFACE_STRING(CommonZoomIn, "Zoom in");
DEFINE_INTERFACE_STRING(CommonZoomOut, "Zoom out");

DEFINE_INTERFACE_STRING(CommonSearchOpen, "Search");
DEFINE_INTERFACE_STRING(CommonSearchNoMatches, "No matches found.");
DEFINE_INTERFACE_STRING(CommonSearchNext, "Find next");
DEFINE_INTERFACE_STRING(CommonSearchPrevious, "Find previous");
DEFINE_INTERFACE_STRING(CommonSearchPrompt, "Search for:");
DEFINE_INTERFACE_STRING(CommonSearchPrompt2, "Enter text to search for.");

DEFINE_INTERFACE_STRING(CommonItemFolder, "Folder");
DEFINE_INTERFACE_STRING(CommonItemFile, "File");

DEFINE_INTERFACE_STRING(CommonSortHeader, "Sort" ELLIPSIS);
DEFINE_INTERFACE_STRING(CommonSortAscending, "Sort ascending");
DEFINE_INTERFACE_STRING(CommonSortDescending, "Sort descending");
DEFINE_INTERFACE_STRING(CommonSortAToZ, "A to Z");
DEFINE_INTERFACE_STRING(CommonSortZToA, "Z to A");
DEFINE_INTERFACE_STRING(CommonSortSmallToLarge, "Smallest first");
DEFINE_INTERFACE_STRING(CommonSortLargeToSmall, "Largest first");
DEFINE_INTERFACE_STRING(CommonSortOldToNew, "Oldest first");
DEFINE_INTERFACE_STRING(CommonSortNewToOld, "Newest first");

DEFINE_INTERFACE_STRING(CommonDriveHDD, "Hard disk");
DEFINE_INTERFACE_STRING(CommonDriveSSD, "SSD");
DEFINE_INTERFACE_STRING(CommonDriveCDROM, "CD-ROM");
DEFINE_INTERFACE_STRING(CommonDriveUSBMassStorage, "USB drive");

DEFINE_INTERFACE_STRING(CommonSystemBrand, SYSTEM_BRAND_SHORT " Alpha v0.1");

DEFINE_INTERFACE_STRING(CommonListViewType, "List view");
DEFINE_INTERFACE_STRING(CommonListViewTypeThumbnails, "Thumbnails");
DEFINE_INTERFACE_STRING(CommonListViewTypeTiles, "Tiles");
DEFINE_INTERFACE_STRING(CommonListViewTypeDetails, "Details");

DEFINE_INTERFACE_STRING(CommonAnnouncementCopied, "Copied");
DEFINE_INTERFACE_STRING(CommonAnnouncementCut, "Cut");
DEFINE_INTERFACE_STRING(CommonAnnouncementTextCopied, "Text copied");
DEFINE_INTERFACE_STRING(CommonAnnouncementCopyErrorResources, "There's not enough space to copy this");
DEFINE_INTERFACE_STRING(CommonAnnouncementCopyErrorOther, "Could not copy");
DEFINE_INTERFACE_STRING(CommonAnnouncementPasteErrorOther, "Could not paste");

DEFINE_INTERFACE_STRING(CommonEmpty, "empty");

DEFINE_INTERFACE_STRING(CommonUnitPercent, "%");
DEFINE_INTERFACE_STRING(CommonUnitBytes, " B");
DEFINE_INTERFACE_STRING(CommonUnitKilobytes, " kB"); // We use this for 10^3, so let's go for the proper SI prefix, rather than the traditional "KB".
DEFINE_INTERFACE_STRING(CommonUnitMegabytes, " MB");
DEFINE_INTERFACE_STRING(CommonUnitGigabytes, " GB");
DEFINE_INTERFACE_STRING(CommonUnitMilliseconds, " ms");
DEFINE_INTERFACE_STRING(CommonUnitSeconds, " s");
DEFINE_INTERFACE_STRING(CommonUnitBits, " bits");
DEFINE_INTERFACE_STRING(CommonUnitPixels, " px");
DEFINE_INTERFACE_STRING(CommonUnitDPI, " dpi");
DEFINE_INTERFACE_STRING(CommonUnitBps, " Bps");
DEFINE_INTERFACE_STRING(CommonUnitKBps, " kBps");
DEFINE_INTERFACE_STRING(CommonUnitMBps, " MBps");
DEFINE_INTERFACE_STRING(CommonUnitHz, " Hz");
DEFINE_INTERFACE_STRING(CommonUnitKHz, " kHz");
DEFINE_INTERFACE_STRING(CommonUnitMHz, " MHz");

DEFINE_INTERFACE_STRING(CommonBooleanYes, "Yes");
DEFINE_INTERFACE_STRING(CommonBooleanNo, "No");
DEFINE_INTERFACE_STRING(CommonBooleanOn, "On");
DEFINE_INTERFACE_STRING(CommonBooleanOff, "Off");

// Desktop.

DEFINE_INTERFACE_STRING(DesktopNewTabTitle, "New Tab");
DEFINE_INTERFACE_STRING(DesktopShutdownTitle, "Shut Down");
DEFINE_INTERFACE_STRING(DesktopShutdownAction, "Shut down");
DEFINE_INTERFACE_STRING(DesktopRestartAction, "Restart");
DEFINE_INTERFACE_STRING(DesktopForceQuit, "Force quit");
DEFINE_INTERFACE_STRING(DesktopCrashedApplication, "The application has crashed. If you're a developer, more information is available in System Monitor.");
DEFINE_INTERFACE_STRING(DesktopNoSuchApplication, "The requested application could not found. It may have been uninstalled.");
DEFINE_INTERFACE_STRING(DesktopApplicationStartupError, "The requested application could not be started. Your system may be low on resources, or the application files may have been corrupted.");
DEFINE_INTERFACE_STRING(DesktopNotResponding, "The application is not responding.\nIf you choose to force quit, any unsaved data may be lost.");
DEFINE_INTERFACE_STRING(DesktopConfirmShutdown, "Are you sure you want to turn off your computer? All applications will be closed.");

DEFINE_INTERFACE_STRING(DesktopCloseTab, "Close tab");
DEFINE_INTERFACE_STRING(DesktopMoveTabToNewWindow, "Move tab to new window");
DEFINE_INTERFACE_STRING(DesktopMoveTabToNewWindowSplitLeft, "Move tab to left of screen");
DEFINE_INTERFACE_STRING(DesktopMoveTabToNewWindowSplitRight, "Move tab to right of screen");
DEFINE_INTERFACE_STRING(DesktopInspectUI, "Inspect UI");
DEFINE_INTERFACE_STRING(DesktopCloseWindow, "Close window");
DEFINE_INTERFACE_STRING(DesktopCloseAllTabs, "Close all tabs");
DEFINE_INTERFACE_STRING(DesktopMaximiseWindow, "Fill screen");
DEFINE_INTERFACE_STRING(DesktopRestoreWindow, "Restore position");
DEFINE_INTERFACE_STRING(DesktopMinimiseWindow, "Hide");
DEFINE_INTERFACE_STRING(DesktopCenterWindow, "Center in screen");
DEFINE_INTERFACE_STRING(DesktopSnapWindowLeft, "Move to left side");
DEFINE_INTERFACE_STRING(DesktopSnapWindowRight, "Move to right side");

DEFINE_INTERFACE_STRING(DesktopSettingsApplication, "Settings");
DEFINE_INTERFACE_STRING(DesktopSettingsTitle, "Settings");
DEFINE_INTERFACE_STRING(DesktopSettingsBackButton, "All settings");
DEFINE_INTERFACE_STRING(DesktopSettingsUndoButton, "Undo changes");
DEFINE_INTERFACE_STRING(DesktopSettingsAccessibility, "Accessibility");
DEFINE_INTERFACE_STRING(DesktopSettingsApplications, "Applications");
DEFINE_INTERFACE_STRING(DesktopSettingsDateAndTime, "Date and time");
DEFINE_INTERFACE_STRING(DesktopSettingsDevices, "Devices");
DEFINE_INTERFACE_STRING(DesktopSettingsDisplay, "Display");
DEFINE_INTERFACE_STRING(DesktopSettingsKeyboard, "Keyboard");
DEFINE_INTERFACE_STRING(DesktopSettingsLocalisation, "Localisation");
DEFINE_INTERFACE_STRING(DesktopSettingsMouse, "Mouse");
DEFINE_INTERFACE_STRING(DesktopSettingsNetwork, "Network");
DEFINE_INTERFACE_STRING(DesktopSettingsPower, "Power");
DEFINE_INTERFACE_STRING(DesktopSettingsSound, "Sound");
DEFINE_INTERFACE_STRING(DesktopSettingsTheme, "Theme");

DEFINE_INTERFACE_STRING(DesktopSettingsKeyboardKeyRepeatDelay, "Key repeat delay:");
DEFINE_INTERFACE_STRING(DesktopSettingsKeyboardKeyRepeatRate, "Key repeat rate:");
DEFINE_INTERFACE_STRING(DesktopSettingsKeyboardCaretBlinkRate, "Caret blink rate:");
DEFINE_INTERFACE_STRING(DesktopSettingsKeyboardTestTextboxIntroduction, "Try your settings in the textbox below:");
DEFINE_INTERFACE_STRING(DesktopSettingsKeyboardUseSmartQuotes, "Use smart quotes when typing");
DEFINE_INTERFACE_STRING(DesktopSettingsKeyboardLayout, "Keyboard layout:");

DEFINE_INTERFACE_STRING(DesktopSettingsMouseDoubleClickSpeed, "Double click time:");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseSpeed, "Cursor movement speed:");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseCursorTrails, "Cursor trail count:");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseLinesPerScrollNotch, "Lines to scroll per wheel notch:");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseSwapLeftAndRightButtons, "Swap left and right buttons");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseShowShadow, "Show shadow below cursor");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseLocateCursorOnCtrl, "Highlight cursor location when Ctrl is pressed");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseTestDoubleClickIntroduction, "Double click the circle below to try your setting. If it does not change color, increase the double click time.");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseUseAcceleration, "Move cursor faster when mouse is moved quickly");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseSlowOnAlt, "Move cursor slower when Alt is held");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseSpeedSlow, "Slow");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseSpeedFast, "Fast");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseCursorTrailsNone, "None");
DEFINE_INTERFACE_STRING(DesktopSettingsMouseCursorTrailsMany, "Many");

DEFINE_INTERFACE_STRING(DesktopSettingsDisplayUIScale, "Interface scale:");

DEFINE_INTERFACE_STRING(DesktopSettingsThemeWindowColor, "Window color:");
DEFINE_INTERFACE_STRING(DesktopSettingsThemeEnableHoverState, "Highlight the item the cursor is over");
DEFINE_INTERFACE_STRING(DesktopSettingsThemeEnableAnimations, "Animate the user interface");
DEFINE_INTERFACE_STRING(DesktopSettingsThemeWallpaper, "Wallpaper:");

// File operations.

DEFINE_INTERFACE_STRING(FileCannotSave, "The document was not saved.");
DEFINE_INTERFACE_STRING(FileCannotOpen, "The file could not be opened.");
DEFINE_INTERFACE_STRING(FileCannotRename, "The file could not be renamed.");

DEFINE_INTERFACE_STRING(FileRenameSuccess, "Renamed");

DEFINE_INTERFACE_STRING(FileSaveAnnouncement, "Saved to %s");
DEFINE_INTERFACE_STRING(FileSaveErrorFileDeleted, "Another application deleted the file.");
DEFINE_INTERFACE_STRING(FileSaveErrorCorrupt, "The file has been corrupted, and it cannot be modified.");
DEFINE_INTERFACE_STRING(FileSaveErrorDrive, "The drive containing the file was unable to modify it.");
DEFINE_INTERFACE_STRING(FileSaveErrorTooLarge, "The drive does not support files large enough to store this document.");
DEFINE_INTERFACE_STRING(FileSaveErrorConcurrentAccess, "Another application is modifying the file.");
DEFINE_INTERFACE_STRING(FileSaveErrorDriveFull, "The drive is full. Try deleting some files to free up space.");
DEFINE_INTERFACE_STRING(FileSaveErrorResourcesLow, "The system is low on resources. Close some applcations and try again.");
DEFINE_INTERFACE_STRING(FileSaveErrorAlreadyExists, "There is already a file called " OPEN_SPEECH "%s" CLOSE_SPEECH " in this folder.");
DEFINE_INTERFACE_STRING(FileSaveErrorTooManyFiles, "Too many files already have the same name.");
DEFINE_INTERFACE_STRING(FileSaveErrorUnknown, "An unknown error occurred. Please try again later.");

DEFINE_INTERFACE_STRING(FileLoadErrorCorrupt, "The file has been corrupted, and it cannot be opened.");
DEFINE_INTERFACE_STRING(FileLoadErrorDrive, "The drive containing the file was unable to access its contents.");
DEFINE_INTERFACE_STRING(FileLoadErrorResourcesLow, "The system is low on resources. Close some applcations and try again.");
DEFINE_INTERFACE_STRING(FileLoadErrorUnknown, "An unknown error occurred. Please try again later.");

DEFINE_INTERFACE_STRING(FileCloseWithModificationsTitle, "Do you want to save this document?");
DEFINE_INTERFACE_STRING(FileCloseWithModificationsContent, "You need to save your changes to " OPEN_SPEECH "%s" CLOSE_SPEECH " before you can close it.");
DEFINE_INTERFACE_STRING(FileCloseWithModificationsSave, "Save and close");
DEFINE_INTERFACE_STRING(FileCloseWithModificationsDelete, "Discard");
DEFINE_INTERFACE_STRING(FileCloseNewTitle, "Do you want to keep this document?");
DEFINE_INTERFACE_STRING(FileCloseNewContent, "You need to save it before you can close " OPEN_SPEECH "%s" CLOSE_SPEECH ".");
DEFINE_INTERFACE_STRING(FileCloseNewName, "Name:");

// Image Editor.

DEFINE_INTERFACE_STRING(ImageEditorToolBrush, "Brush");
DEFINE_INTERFACE_STRING(ImageEditorToolFill, "Fill");
DEFINE_INTERFACE_STRING(ImageEditorToolRectangle, "Rectangle");
DEFINE_INTERFACE_STRING(ImageEditorToolSelect, "Select");
DEFINE_INTERFACE_STRING(ImageEditorToolText, "Text");

DEFINE_INTERFACE_STRING(ImageEditorCanvasSize, "Canvas size");

DEFINE_INTERFACE_STRING(ImageEditorPropertyWidth, "Width:");
DEFINE_INTERFACE_STRING(ImageEditorPropertyHeight, "Height:");
DEFINE_INTERFACE_STRING(ImageEditorPropertyColor, "Color:");
DEFINE_INTERFACE_STRING(ImageEditorPropertyBrushSize, "Brush size:");

DEFINE_INTERFACE_STRING(ImageEditorImageTransformations, "Transform image");
DEFINE_INTERFACE_STRING(ImageEditorRotateLeft, "Rotate left");
DEFINE_INTERFACE_STRING(ImageEditorRotateRight, "Rotate right");
DEFINE_INTERFACE_STRING(ImageEditorFlipHorizontally, "Flip horizontally");
DEFINE_INTERFACE_STRING(ImageEditorFlipVertically, "Flip vertically");

DEFINE_INTERFACE_STRING(ImageEditorImage, "Image");
DEFINE_INTERFACE_STRING(ImageEditorPickTool, "Pick tool");

DEFINE_INTERFACE_STRING(ImageEditorUnsupportedFormat, "The image is in an unsupported format. Try opening it with another application.");

DEFINE_INTERFACE_STRING(ImageEditorNewFileName, "untitled.png");
DEFINE_INTERFACE_STRING(ImageEditorNewDocument, "New bitmap image");

DEFINE_INTERFACE_STRING(ImageEditorTitle, "Image Editor");

// Text Editor.

DEFINE_INTERFACE_STRING(TextEditorTitle, "Text Editor");
DEFINE_INTERFACE_STRING(TextEditorNewFileName, "untitled.txt");
DEFINE_INTERFACE_STRING(TextEditorNewDocument, "New text document");

// Markdown Viewer.

DEFINE_INTERFACE_STRING(MarkdownViewerTitle, "Markdown Viewer");

// POSIX.

DEFINE_INTERFACE_STRING(POSIXUnavailable, "This application depends on the POSIX subsystem. To enable it, select \am]Flag.ENABLE_POSIX_SUBSYSTEM\a] in \am]config\a].");
DEFINE_INTERFACE_STRING(POSIXTitle, "POSIX Application");

// Font Book.

DEFINE_INTERFACE_STRING(FontBookTitle, "Font Book");
DEFINE_INTERFACE_STRING(FontBookTextSize, "Text size:");
DEFINE_INTERFACE_STRING(FontBookPreviewText, "Preview text:");
DEFINE_INTERFACE_STRING(FontBookVariants, "Variants");
DEFINE_INTERFACE_STRING(FontBookPreviewTextDefault, "Looking for a change of mind.");
DEFINE_INTERFACE_STRING(FontBookPreviewTextLongDefault, "Sphinx of black quartz, judge my vow.");
DEFINE_INTERFACE_STRING(FontBookOpenFont, "Open");
DEFINE_INTERFACE_STRING(FontBookNavigationBack, "Back to all fonts");
DEFINE_INTERFACE_STRING(FontBookVariantNormal100, "Thin");
DEFINE_INTERFACE_STRING(FontBookVariantNormal200, "Extra light");
DEFINE_INTERFACE_STRING(FontBookVariantNormal300, "Light");
DEFINE_INTERFACE_STRING(FontBookVariantNormal400, "Normal");
DEFINE_INTERFACE_STRING(FontBookVariantNormal500, "Medium");
DEFINE_INTERFACE_STRING(FontBookVariantNormal600, "Semi bold");
DEFINE_INTERFACE_STRING(FontBookVariantNormal700, "Bold");
DEFINE_INTERFACE_STRING(FontBookVariantNormal800, "Extra bold");
DEFINE_INTERFACE_STRING(FontBookVariantNormal900, "Black");
DEFINE_INTERFACE_STRING(FontBookVariantItalic100, "Thin (italic)");
DEFINE_INTERFACE_STRING(FontBookVariantItalic200, "Extra light (italic)");
DEFINE_INTERFACE_STRING(FontBookVariantItalic300, "Light (italic)");
DEFINE_INTERFACE_STRING(FontBookVariantItalic400, "Normal (italic)");
DEFINE_INTERFACE_STRING(FontBookVariantItalic500, "Medium (italic)");
DEFINE_INTERFACE_STRING(FontBookVariantItalic600, "Semi bold (italic)");
DEFINE_INTERFACE_STRING(FontBookVariantItalic700, "Bold (italic)");
DEFINE_INTERFACE_STRING(FontBookVariantItalic800, "Extra bold (italic)");
DEFINE_INTERFACE_STRING(FontBookVariantItalic900, "Black (italic)");

// File Manager.

DEFINE_INTERFACE_STRING(FileManagerOpenFolderError, "The folder could not be opened.");
DEFINE_INTERFACE_STRING(FileManagerNewFolderError, "Could not create the folder.");
DEFINE_INTERFACE_STRING(FileManagerRenameItemError, "The item could not be renamed.");
DEFINE_INTERFACE_STRING(FileManagerUnknownError, "An unknown error occurred.");
DEFINE_INTERFACE_STRING(FileManagerTitle, "File Manager");
DEFINE_INTERFACE_STRING(FileManagerRootFolder, "Computer");
DEFINE_INTERFACE_STRING(FileManagerColumnName, "Name");
DEFINE_INTERFACE_STRING(FileManagerColumnType, "Type");
DEFINE_INTERFACE_STRING(FileManagerColumnSize, "Size");
DEFINE_INTERFACE_STRING(FileManagerOpenFolderTask, "Opening folder" ELLIPSIS);
DEFINE_INTERFACE_STRING(FileManagerOpenFileError, "The file could not be opened.");
DEFINE_INTERFACE_STRING(FileManagerNoRegisteredApplicationsForFile, "None of the applications installed on this computer can open this type of file.");
DEFINE_INTERFACE_STRING(FileManagerCannotOpenSystemFile, "This is a system file which has already been loaded.");
DEFINE_INTERFACE_STRING(FileManagerFolderNamePrompt, "Folder name:");
DEFINE_INTERFACE_STRING(FileManagerNewFolderAction, "Create");
DEFINE_INTERFACE_STRING(FileManagerNewFolderTask, "Creating folder" ELLIPSIS);
DEFINE_INTERFACE_STRING(FileManagerRenameTitle, "Rename");
DEFINE_INTERFACE_STRING(FileManagerRenamePrompt, "Type the new name of the item:");
DEFINE_INTERFACE_STRING(FileManagerRenameAction, "Rename");
DEFINE_INTERFACE_STRING(FileManagerRenameTask, "Renaming item" ELLIPSIS);
DEFINE_INTERFACE_STRING(FileManagerEmptyBookmarkView, "Drag folders here to bookmark them.");
DEFINE_INTERFACE_STRING(FileManagerEmptyFolderView, "Drag items here to add them to the folder.");
DEFINE_INTERFACE_STRING(FileManagerNewFolderToolbarItem, "New folder");
DEFINE_INTERFACE_STRING(FileManagerNewFolderName, "New folder");
DEFINE_INTERFACE_STRING(FileManagerGenericError, "The cause of the error could not be identified.");
DEFINE_INTERFACE_STRING(FileManagerItemAlreadyExistsError, "The item already exists in the folder.");
DEFINE_INTERFACE_STRING(FileManagerItemDoesNotExistError, "The item does not exist.");
DEFINE_INTERFACE_STRING(FileManagerPermissionNotGrantedError, "You don't have permission to modify this folder.");
DEFINE_INTERFACE_STRING(FileManagerOngoingTaskDescription, "This shouldn't take long.");
DEFINE_INTERFACE_STRING(FileManagerPlacesDrives, "Drives");
DEFINE_INTERFACE_STRING(FileManagerPlacesBookmarks, "Bookmarks");
DEFINE_INTERFACE_STRING(FileManagerBookmarksAddHere, "Add bookmark here");
DEFINE_INTERFACE_STRING(FileManagerBookmarksRemoveHere, "Remove bookmark here");
DEFINE_INTERFACE_STRING(FileManagerDrivesPage, "Drives/");
DEFINE_INTERFACE_STRING(FileManagerInvalidPath, "The current path does not lead to a folder. It may have been deleted or moved.");
DEFINE_INTERFACE_STRING(FileManagerInvalidDrive, "The drive containing this folder was disconnected.");
DEFINE_INTERFACE_STRING(FileManagerRefresh, "Refresh");
DEFINE_INTERFACE_STRING(FileManagerListContextActions, "Actions");
DEFINE_INTERFACE_STRING(FileManagerCopyTask, "Copying" ELLIPSIS);
DEFINE_INTERFACE_STRING(FileManagerMoveTask, "Moving" ELLIPSIS);
DEFINE_INTERFACE_STRING(FileManagerGoBack, "Go back");
DEFINE_INTERFACE_STRING(FileManagerGoForwards, "Go forwards");
DEFINE_INTERFACE_STRING(FileManagerGoUp, "Go to containing folder");
DEFINE_INTERFACE_STRING(FileManagerFileOpenIn, "File is open in " OPEN_SPEECH "%s" CLOSE_SPEECH);

// 2048.

DEFINE_INTERFACE_STRING(Game2048Score, "Score:");
DEFINE_INTERFACE_STRING(Game2048Instructions, "Use the \aw6]arrow-keys\a] to slide the tiles. When matching tiles touch, they \aw6]merge\a] into one. Try to create the number \aw6]2048\a]!");
DEFINE_INTERFACE_STRING(Game2048GameOver, "Game over");
DEFINE_INTERFACE_STRING(Game2048GameOverExplanation, "There are no valid moves left.");
DEFINE_INTERFACE_STRING(Game2048NewGame, "New game");
DEFINE_INTERFACE_STRING(Game2048HighScore, "High score: \aw6]%d\a]");
DEFINE_INTERFACE_STRING(Game2048NewHighScore, "You reached a new high score!");

// Installer.

DEFINE_INTERFACE_STRING(InstallerTitle, "Install " SYSTEM_BRAND_SHORT);
DEFINE_INTERFACE_STRING(InstallerDrivesList, "Select the drive to install on:");
DEFINE_INTERFACE_STRING(InstallerDrivesSelectHint, "Choose a drive from the list on the left.");
DEFINE_INTERFACE_STRING(InstallerDriveRemoved, "The drive was disconnected.");
DEFINE_INTERFACE_STRING(InstallerDriveReadOnly, "This drive is read-only. You cannot install " SYSTEM_BRAND_SHORT " on this drive.");
DEFINE_INTERFACE_STRING(InstallerDriveNotEnoughSpace, "This drive does not have enough space to install " SYSTEM_BRAND_SHORT ".");
DEFINE_INTERFACE_STRING(InstallerDriveCouldNotRead, "The drive could not be accessed. It may not be working correctly.");
DEFINE_INTERFACE_STRING(InstallerDriveAlreadyHasPartitions, "The drive already has data on it. You cannot install " SYSTEM_BRAND_SHORT " on this drive.");
DEFINE_INTERFACE_STRING(InstallerDriveUnsupported, "This drive uses unsupported features. You cannot install " SYSTEM_BRAND_SHORT " on this drive.");
DEFINE_INTERFACE_STRING(InstallerDriveOkay, SYSTEM_BRAND_SHORT " can be installed on this drive.");
DEFINE_INTERFACE_STRING(InstallerInstall, "Install");
DEFINE_INTERFACE_STRING(InstallerViewLicenses, "Licenses");
DEFINE_INTERFACE_STRING(InstallerGoBack, "Back");
DEFINE_INTERFACE_STRING(InstallerFinish, "Finish");
DEFINE_INTERFACE_STRING(InstallerCustomizeOptions, "Customize your computer.");
DEFINE_INTERFACE_STRING(InstallerCustomizeOptionsHint, "More options will be available in Settings.");
DEFINE_INTERFACE_STRING(InstallerUserName, "User name:");
DEFINE_INTERFACE_STRING(InstallerTime, "Current time:");
DEFINE_INTERFACE_STRING(InstallerSystemFont, "System font:");
DEFINE_INTERFACE_STRING(InstallerFontDefault, "Default");
DEFINE_INTERFACE_STRING(InstallerProgressMessage, "Installing, please wait" ELLIPSIS "\nDo not turn off your computer.\nProgress: \aw6]");
DEFINE_INTERFACE_STRING(InstallerCompleteFromOther, "Installation has completed successfully. Remove the installation disk, and restart your computer.");
DEFINE_INTERFACE_STRING(InstallerCompleteFromUSB, "Installation has completed successfully. Disconnect the installation USB, and restart your computer.");
DEFINE_INTERFACE_STRING(InstallerVolumeLabel, "Essence HD");
DEFINE_INTERFACE_STRING(InstallerUseMBR, "Use legacy BIOS boot (select for older computers)");
DEFINE_INTERFACE_STRING(InstallerFailedArchiveCRCError, "The installation data has been corrupted. Create a new installation USB or disk, and try again.");
DEFINE_INTERFACE_STRING(InstallerFailedGeneric, "The installation could not complete. This likely means that the drive you selected is failing. Try installing on a different drive.");
DEFINE_INTERFACE_STRING(InstallerFailedResources, "The installation could not complete. Your computer does not have enough memory to install " SYSTEM_BRAND_SHORT);
DEFINE_INTERFACE_STRING(InstallerNotSupported, "Your computer does not meet the minimum system requirements to install " SYSTEM_BRAND_SHORT ". Remove the installer, and restart your computer.");

// Build Core.

DEFINE_INTERFACE_STRING(BuildCoreTitle, "Build Core");
DEFINE_INTERFACE_STRING(BuildCoreNoConfigFileLoaded, "No config file is loaded.");
DEFINE_INTERFACE_STRING(BuildCoreNoFilePath, "The config file is not in a real folder, so it can't be loaded.");
DEFINE_INTERFACE_STRING(BuildCorePathCannotBeAccessedByPOSIXSubsystem, "The config file is not located on the 0:/ drive, so it can't be accessed by the POSIX subsystem.");
DEFINE_INTERFACE_STRING(BuildCoreBuild, "Build");
DEFINE_INTERFACE_STRING(BuildCoreLaunch, "Launch");
DEFINE_INTERFACE_STRING(BuildCoreCannotCreateBuildThread, "The build thread could not be created.");
DEFINE_INTERFACE_STRING(BuildCoreBuildFailed, "\n--- The build failed. ---\n");
DEFINE_INTERFACE_STRING(BuildCoreBuildSuccess, "\n(success)\n");

// TODO System Monitor.

#pragma GCC diagnostic pop
