#pragma GCC diagnostic ignored "-Wunused-variable" push

#define DEFINE_INTERFACE_STRING(name, text) static const char *interfaceString_ ## name = text;
#define INTERFACE_STRING(name) interfaceString_ ## name, -1

#define ELLIPSIS "…"
#define HYPHENATION_POINT "‧"

// Common.

DEFINE_INTERFACE_STRING(CommonErrorTitle, "Error");

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
DEFINE_INTERFACE_STRING(CommonFileUnchanged, "(All changes saved.)");

DEFINE_INTERFACE_STRING(CommonSearchOpen, "Search");
DEFINE_INTERFACE_STRING(CommonSearchNoMatches, "No matches found.");
DEFINE_INTERFACE_STRING(CommonSearchNext, "Find next");
DEFINE_INTERFACE_STRING(CommonSearchPrevious, "Find previous");
DEFINE_INTERFACE_STRING(CommonSearchPrompt, "Search for:");
DEFINE_INTERFACE_STRING(CommonSearchPrompt2, "Enter text to search for.");

DEFINE_INTERFACE_STRING(CommonItemFolder, "Folder");
DEFINE_INTERFACE_STRING(CommonItemFile, "File");

DEFINE_INTERFACE_STRING(CommonSortAscending, "Sort ascending");
DEFINE_INTERFACE_STRING(CommonSortDescending, "Sort descending");

DEFINE_INTERFACE_STRING(CommonDriveHDD, "Hard disk");
DEFINE_INTERFACE_STRING(CommonDriveSSD, "SSD");
DEFINE_INTERFACE_STRING(CommonDriveCDROM, "CD-ROM");
DEFINE_INTERFACE_STRING(CommonDriveUSBMassStorage, "USB drive");

DEFINE_INTERFACE_STRING(CommonSystemBrand, "Essence Alpha v0.1");

DEFINE_INTERFACE_STRING(CommonListViewType, "List view");
DEFINE_INTERFACE_STRING(CommonListViewTypeThumbnails, "Thumbnails");
DEFINE_INTERFACE_STRING(CommonListViewTypeTiles, "Tiles");
DEFINE_INTERFACE_STRING(CommonListViewTypeDetails, "Details");

DEFINE_INTERFACE_STRING(CommonAnnouncementTextCopied, "Text copied");
DEFINE_INTERFACE_STRING(CommonAnnouncementCopyErrorResources, "There's not enough space to copy this");
DEFINE_INTERFACE_STRING(CommonAnnouncementCopyErrorOther, "Could not copy");

// Desktop.

DEFINE_INTERFACE_STRING(DesktopCloseTab, "Close tab");
DEFINE_INTERFACE_STRING(DesktopInspectUI, "Inspect UI");
DEFINE_INTERFACE_STRING(DesktopCenterWindow, "Center in screen");
DEFINE_INTERFACE_STRING(DesktopNewTabTitle, "New Tab");
DEFINE_INTERFACE_STRING(DesktopShutdownTitle, "Shutdown");
DEFINE_INTERFACE_STRING(DesktopShutdownAction, "Shutdown");
DEFINE_INTERFACE_STRING(DesktopRestartAction, "Restart");
DEFINE_INTERFACE_STRING(DesktopForceQuit, "Force quit");
DEFINE_INTERFACE_STRING(DesktopCrashedApplication, "The application has crashed. If you're a developer, more information is available in System Monitor.");
DEFINE_INTERFACE_STRING(DesktopNoSuchApplication, "The requested application could not found. It may have been uninstalled.");
DEFINE_INTERFACE_STRING(DesktopApplicationStartupError, "The requested application could not be started. Your system may be low on resources, or the application files may have been corrupted.");
DEFINE_INTERFACE_STRING(DesktopNotResponding, "The application is not responding.\nIf you choose to force quit, any unsaved data may be lost.");
DEFINE_INTERFACE_STRING(DesktopConfirmShutdown, "Are you sure you want to turn off your computer? All applications will be closed.");
DEFINE_INTERFACE_STRING(DesktopSettingsApplication, "Settings");
DEFINE_INTERFACE_STRING(DesktopSettingsTitle, "Settings");

// File operations.

DEFINE_INTERFACE_STRING(FileCannotSave, "The document was not saved.");
DEFINE_INTERFACE_STRING(FileCannotOpen, "The file could not be opened.");

DEFINE_INTERFACE_STRING(FileSaveErrorFileDeleted, "Another application deleted the file.");
DEFINE_INTERFACE_STRING(FileSaveErrorCorrupt, "The file has been corrupted, and it cannot be modified.");
DEFINE_INTERFACE_STRING(FileSaveErrorDrive, "The drive containing the file was unable to modify it.");
DEFINE_INTERFACE_STRING(FileSaveErrorTooLarge, "The drive does not support files large enough to store this document.");
DEFINE_INTERFACE_STRING(FileSaveErrorConcurrentAccess, "Another application is modifying the file.");
DEFINE_INTERFACE_STRING(FileSaveErrorDriveFull, "The drive is full. Try deleting some files to free up space.");
DEFINE_INTERFACE_STRING(FileSaveErrorResourcesLow, "The system is low on resources. Close some applcations and try again.");
DEFINE_INTERFACE_STRING(FileSaveErrorAlreadyExists, "Too many files already have the same name.");
DEFINE_INTERFACE_STRING(FileSaveErrorUnknown, "An unknown error occurred. Please try again later.");

DEFINE_INTERFACE_STRING(FileLoadErrorCorrupt, "The file has been corrupted, and it cannot be opened.");
DEFINE_INTERFACE_STRING(FileLoadErrorDrive, "The drive containing the file was unable to access its contents.");
DEFINE_INTERFACE_STRING(FileLoadErrorResourcesLow, "The system is low on resources. Close some applcations and try again.");
DEFINE_INTERFACE_STRING(FileLoadErrorUnknown, "An unknown error occurred. Please try again later.");

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

// TODO System Monitor.

#pragma GCC diagnostic pop
