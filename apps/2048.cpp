#include <essence.h>
#include <shared/strings.cpp>

// TODO Application icon.

#define TILE_COUNT (4)
#define TILE_SIZE ((int32_t) (75 * scale))
#define TILE_GAP ((int32_t) (10 * scale))
#define CELL_FILL (0xFFEEEEEE)
#define TILE_TEXT_COLOR (0xFFFFFF)
#define TILE_TEXT_GLOW (0x000000)
#define MAIN_AREA_SIZE() (TILE_SIZE * TILE_COUNT + TILE_GAP * (TILE_COUNT + 1))
#define MAIN_AREA_FILL (0xFFFFFFFF)
#define MAIN_AREA_BORDER (0xFFCCCCCC)
#define ANIMATION_TIME (100)

#define TILE_BOUNDS(x, y) ES_RECT_4PD(mainArea.l + TILE_GAP * (x + 1) + TILE_SIZE * x, mainArea.t + TILE_GAP * (y + 1) + TILE_SIZE * y, TILE_SIZE, TILE_SIZE)

#define SETTINGS_FILE "|Settings:/Default.dat"

struct AnimatingTile {
	float sourceOpacity, targetOpacity;
	uint8_t sourceX, targetX;
	uint8_t sourceY, targetY;
	uint8_t sourceNumber;
};

const uint32_t tileColors[] = {
	0x000000, 0x7CB5E2, 0x4495D4, 0x2F6895,
	0xF5BD70, 0xF2A032, 0xE48709, 0xE37051,
	0xDE5833, 0xBD4A2B, 0x5454DA, 0x3B3C99,
	0xFFD700,
};

const uint32_t tileTextSizes[] = {
	0, 18, 18, 18, 18, 18, 18, 18,
	18, 18, 16, 16, 16, 16, 14, 14, 
	14, 12, 12, 12, 10, 10, 10, 10,
	8, 8, 8, 6, 6, 6, 6, 6,
};

AnimatingTile animatingTiles[TILE_COUNT * TILE_COUNT + 1];
size_t animatingTileCount;
float animationTimeMs;

uint8_t grid[TILE_COUNT][TILE_COUNT];
int32_t score, highScore;

EsInstance *instance;
EsElement *gameArea;
EsTextDisplay *scoreDisplay, *highScoreDisplay;

void SaveConfiguration() {
	EsBuffer buffer = {};
	buffer.canGrow = true;
	EsBufferWriteInt32Endian(&buffer, highScore);
	EsBufferWriteInt32Endian(&buffer, score);
	EsBufferWrite(&buffer, grid, sizeof(grid));
	EsFileWriteAll(EsLiteral(SETTINGS_FILE), buffer.out, buffer.position);
	EsHeapFree(buffer.out);
}

bool MoveTiles(intptr_t dx, intptr_t dy, bool speculative) {
	uint8_t undo[TILE_COUNT][TILE_COUNT];
	EsMemoryCopy(undo, grid, sizeof(undo));

	bool validMove = false;

	for (uintptr_t p = 0; p < TILE_COUNT; p++) {
		bool doneMerge = false;

		for (uintptr_t q = 0; q < TILE_COUNT; q++) {
			// The tile being moved.
			intptr_t x = dx ? q : p;
			intptr_t y = dx ? p : q;
			if (dx > 0) x = TILE_COUNT - 1 - x;
			if (dy > 0) y = TILE_COUNT - 1 - y;

			// Ignore empty spaces.
			if (!grid[x][y]) continue;

			// Setup the animation.
			if (!speculative) {
				AnimatingTile *animation = &animatingTiles[animatingTileCount];
				animation->sourceOpacity = 1;
				animation->targetOpacity = 1;
				animation->sourceX = x;
				animation->sourceY = y;
				animation->sourceNumber = grid[x][y];
			}

			while (true) {
				// The position to move the tile to.
				intptr_t nx = x + dx;
				intptr_t ny = y + dy;

				// If the next position is outside the grid, stop.
				if (nx < 0 || nx >= TILE_COUNT) break;
				if (ny < 0 || ny >= TILE_COUNT) break;

				if (grid[nx][ny]) {
					// If tiles are different, we can't merge; stop.
					if (grid[nx][ny] != grid[x][y]) break;

					// If there's already been a merge this band, stop.
					if (doneMerge) break;

					// Merge the tiles.
					grid[nx][ny]++;
					grid[x][y] = 0;
					doneMerge = true;

					// Add the score.
					if (!speculative) score += 1 << grid[nx][ny];
				} else {
					// Slide the tile.
					grid[nx][ny] = grid[x][y];
					grid[x][y] = 0;
				}

				// Update the position.
				x = nx;
				y = ny;
				validMove = true;
			}

			// Set the animation target.
			if (!speculative) {
				AnimatingTile *animation = &animatingTiles[animatingTileCount];
				animation->targetX = x;
				animation->targetY = y;
				animatingTileCount++;
			}
		}
	}

	if (speculative) {
		EsMemoryCopy(grid, undo, sizeof(undo));
	}

	return validMove;
}

void SpawnTile() {
	bool full = true;

	for (uintptr_t i = 0; i < TILE_COUNT; i++) {
		for (uintptr_t j = 0; j < TILE_COUNT; j++) {
			if (!grid[i][j]) {
				full = false;
			}
		}
	}

	if (full) {
		// The grid is full.
		return;
	}
		
	while (true) {
		uintptr_t x = EsRandomU64() % TILE_COUNT;
		uintptr_t y = EsRandomU64() % TILE_COUNT;

		if (!grid[x][y]) {
			grid[x][y] = EsRandomU8() < 25 ? 2 : 1;

			// Setup the animation.
			AnimatingTile *animation = &animatingTiles[animatingTileCount];
			animation->sourceOpacity = 0;
			animation->targetOpacity = 1;
			animation->sourceX = x;
			animation->targetX = x;
			animation->sourceY = y;
			animation->targetY = y;
			animation->sourceNumber = grid[x][y];
			animatingTileCount++;

			break;
		}
	}

	if (!MoveTiles(-1, 0, true) && !MoveTiles(1, 0, true) && !MoveTiles(0, -1, true) && !MoveTiles(0, 1, true)) {
		// No moves are possible.
		if (highScore < score) {
			EsDialogShowAlert(instance->window, INTERFACE_STRING(Game2048GameOver), INTERFACE_STRING(Game2048NewHighScore), 
					ES_ICON_DIALOG_INFORMATION, ES_DIALOG_ALERT_OK_BUTTON);
		} else {
			EsDialogShowAlert(instance->window, INTERFACE_STRING(Game2048GameOver), INTERFACE_STRING(Game2048GameOverExplanation), 
					ES_ICON_DIALOG_INFORMATION, ES_DIALOG_ALERT_OK_BUTTON);
		}
	}
}

void Update(intptr_t dx, intptr_t dy) {
	animatingTileCount = 0;
	animationTimeMs = 0;

	if (dx || dy) {
		if (!MoveTiles(dx, dy, false)) {
			return;
		}

		SpawnTile();
	}

	EsElementStartAnimating(gameArea);

	if (score > highScore) {
		highScore = score;
	}

	char buffer[64];
	size_t bytes = EsStringFormat(buffer, sizeof(buffer), "%d", score);
	EsTextDisplaySetContents(scoreDisplay, buffer, bytes);
	bytes = EsStringFormat(buffer, sizeof(buffer), interfaceString_Game2048HighScore, highScore);
	EsTextDisplaySetContents(highScoreDisplay, buffer, bytes);
}

void DrawTileText(EsPainter *painter, EsElement *element, EsRectangle bounds, float opacity, uint8_t number) {
	char buffer[64];
	size_t bytes = EsStringFormat(buffer, sizeof(buffer), "%d", 1 << (uint32_t) number);
	uint32_t alpha = ((uint32_t) (255 * opacity) << 24);
	EsTextStyle style = { .font = { .family = ES_FONT_SANS, .weight = ES_FONT_SEMIBOLD }, .size = (uint16_t) tileTextSizes[number] };

	if (number >= 12) {
		style.color = TILE_TEXT_GLOW | alpha;
		style.blur = 3;
		EsDrawTextSimple(painter, element, bounds, buffer, bytes, style, ES_TEXT_H_CENTER | ES_TEXT_V_CENTER);
	}

	style.color = TILE_TEXT_COLOR | alpha;
	style.blur = 0;
	EsDrawTextSimple(painter, element, bounds, buffer, bytes, style, ES_TEXT_H_CENTER | ES_TEXT_V_CENTER);
}

void DrawTile(EsPainter *painter, EsElement *element, uint8_t sourceNumber, uint8_t targetNumber, 
		EsRectangle bounds, float opacity, const uint32_t *cornerRadii, float progress) {
	size_t tileColorCount = sizeof(tileColors) / sizeof(tileColors[0]);
	uint32_t sourceColor = sourceNumber >= tileColorCount ? tileColors[tileColorCount - 1] : tileColors[sourceNumber];
	uint32_t targetColor = targetNumber >= tileColorCount ? tileColors[tileColorCount - 1] : tileColors[targetNumber];
	uint32_t fill = EsColorInterpolate(sourceColor, targetColor, progress) | ((uint32_t) (255 * opacity) << 24);
	float scale = EsElementGetScaleFactor(element);

	EsDrawRoundedRectangle(painter, bounds, fill, EsColorBlend(fill, 0x20000000, true), ES_RECT_4(0, 0, 0, 3 * scale), cornerRadii);

	if (sourceNumber == targetNumber) {
		progress = 1.0f;
	}

	DrawTileText(painter, element, bounds, progress, targetNumber);

	if (sourceNumber != targetNumber && targetNumber) {
		DrawTileText(painter, element, bounds, 1.0f - progress, sourceNumber);
	}
}

int GameAreaMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_PAINT) {
		EsPainter *painter = message->painter;
		float scale = EsElementGetScaleFactor(element);

		const uint32_t cornerRadii[4] = { (uint32_t) (3 * scale), (uint32_t) (3 * scale), (uint32_t) (3 * scale), (uint32_t) (3 * scale) };

		EsRectangle bounds = EsPainterBoundsInset(painter);
		EsRectangle mainArea = EsRectangleFit(bounds, ES_RECT_1S(MAIN_AREA_SIZE()), false);
		EsDrawRoundedRectangle(painter, mainArea, MAIN_AREA_FILL, MAIN_AREA_BORDER, ES_RECT_1(scale * 1), cornerRadii);

		float progress = animationTimeMs / ANIMATION_TIME;
		bool animationComplete = progress == 1.0;

		progress -= 1.0;
		progress = 1 + progress * progress * progress;

		for (uintptr_t j = 0; j < TILE_COUNT; j++) {
			for (uintptr_t i = 0; i < TILE_COUNT; i++) {
				if (grid[i][j] && animationComplete) {
					DrawTile(painter, element, grid[i][j], grid[i][j], TILE_BOUNDS(i, j), 1.0f, cornerRadii, 1.0f);
				} else {
					EsDrawRoundedRectangle(painter, TILE_BOUNDS(i, j), CELL_FILL, 0, ES_RECT_1(0), cornerRadii);
				}
			}
		}

		if (!animationComplete) {
			for (uintptr_t i = 0; i < animatingTileCount; i++) {
				AnimatingTile *tile = &animatingTiles[i];
				EsRectangle bounds = EsRectangleLinearInterpolate(TILE_BOUNDS(tile->sourceX, tile->sourceY), TILE_BOUNDS(tile->targetX, tile->targetY), progress);
				float opacity = (tile->targetOpacity - tile->sourceOpacity) * progress + tile->sourceOpacity;
				DrawTile(painter, element, tile->sourceNumber, grid[tile->targetX][tile->targetY], bounds, opacity, cornerRadii, progress);
			}
		}
	} else if (message->type == ES_MSG_ANIMATE) {
		animationTimeMs += message->animate.deltaMs;

		if (animationTimeMs > ANIMATION_TIME) {
			animationTimeMs = ANIMATION_TIME;
			message->animate.complete = true;
		} else {
			message->animate.complete = false;
		}

		EsElementRepaint(element);
	} else if (message->type == ES_MSG_KEY_TYPED) {
		if (message->keyboard.scancode == ES_SCANCODE_LEFT_ARROW) {
			Update(-1, 0);
		} else if (message->keyboard.scancode == ES_SCANCODE_RIGHT_ARROW) {
			Update(1, 0);
		} else if (message->keyboard.scancode == ES_SCANCODE_UP_ARROW) {
			Update(0, -1);
		} else if (message->keyboard.scancode == ES_SCANCODE_DOWN_ARROW) {
			Update(0, 1);
		} else {
			return 0;
		}
	} else if (message->type == ES_MSG_GET_WIDTH || message->type == ES_MSG_GET_HEIGHT) {
		float scale = EsElementGetScaleFactor(element);
		message->measure.width = message->measure.height = MAIN_AREA_SIZE();
	} else {
		return 0;
	}

	return ES_HANDLED;
}

int InfoPanelMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_GET_WIDTH || message->type == ES_MSG_GET_HEIGHT) {
		float scale = EsElementGetScaleFactor(element);
		message->measure.width = MAIN_AREA_SIZE() / 2;
		message->measure.height = MAIN_AREA_SIZE();
	} else {
		return 0;
	}

	return ES_HANDLED;
}

void NewGameCommand(EsInstance *, EsElement *, EsCommand *) {
	SaveConfiguration();
	EsElementStartTransition(gameArea, ES_TRANSITION_SLIDE_UP);
	EsMemoryZero(grid, sizeof(grid));
	score = 0;
	Update(0, 0);
	SpawnTile();
	EsElementFocus(gameArea);
}

void ProcessApplicationMessage(EsMessage *message) {
	if (message->type == ES_MSG_INSTANCE_CREATE) {
		// Create the instance.
		instance = EsInstanceCreate(message, EsLiteral("2048"));
		EsWindowSetIcon(instance->window, ES_ICON_APPLICATIONS_OTHER);

		// Main horizontal stack.
		EsPanel *panel = EsPanelCreate(instance->window, ES_CELL_FILL | ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_WINDOW_BACKGROUND);
		EsSpacerCreate(panel, ES_CELL_FILL);
		gameArea = EsCustomElementCreate(panel, ES_ELEMENT_FOCUSABLE);
		gameArea->messageUser = GameAreaMessage;
		EsSpacerCreate(panel, ES_FLAGS_DEFAULT, nullptr, 30, 0);
		EsPanel *info = EsPanelCreate(panel);
		EsSpacerCreate(panel, ES_CELL_FILL);

		// Info panel.
		info->messageUser = InfoPanelMessage;
		EsTextDisplayCreate(info, ES_CELL_H_FILL, ES_STYLE_TEXT_LABEL_SECONDARY, INTERFACE_STRING(Game2048Score));
		scoreDisplay = EsTextDisplayCreate(info, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING0);
		EsSpacerCreate(info, ES_FLAGS_DEFAULT, nullptr, 0, 10);
		highScoreDisplay = EsTextDisplayCreate(info, ES_CELL_H_FILL | ES_TEXT_DISPLAY_RICH_TEXT, ES_STYLE_TEXT_LABEL_SECONDARY);
		EsSpacerCreate(info, ES_CELL_FILL);
		EsTextDisplayCreate(info, ES_CELL_H_FILL | ES_TEXT_DISPLAY_RICH_TEXT, ES_STYLE_TEXT_PARAGRAPH_SECONDARY, INTERFACE_STRING(Game2048Instructions));
		EsSpacerCreate(info, ES_FLAGS_DEFAULT, nullptr, 0, 10);
		EsButton *newGame = EsButtonCreate(info, ES_CELL_H_LEFT | ES_BUTTON_NOT_FOCUSABLE, nullptr, INTERFACE_STRING(Game2048NewGame));
		newGame->accessKey = 'N';
		EsButtonOnCommand(newGame, NewGameCommand);

		// Start the game!
		EsElementFocus(gameArea);
		Update(0, 0);
		animationTimeMs = ANIMATION_TIME;
	} else if (message->type == ES_MSG_APPLICATION_EXIT) {
		SaveConfiguration();
	}
}

void _start() {
	_init();

	EsBuffer buffer = {};
	uint8_t *settings = (uint8_t *) EsFileReadAll(EsLiteral(SETTINGS_FILE), &buffer.bytes);
	buffer.in = settings;
	highScore = EsBufferReadInt32Endian(&buffer, 0);
	score = EsBufferReadInt32Endian(&buffer, 0);
	EsBufferReadInto(&buffer, grid, sizeof(grid));
	EsHeapFree(settings);
	if (!settings) SpawnTile();

	while (true) {
		ProcessApplicationMessage(EsMessageReceive());
	}
}
