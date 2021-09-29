#include <essence.h>

EsInstance *instance;
EsElement *canvas;

float spriteX;
float spriteY;

bool isLeftHeld;
bool isRightHeld;
bool isUpHeld;
bool isDownHeld;

void GameRender(EsPainter *painter, EsRectangle bounds) {
	// Fill with black.
	EsDrawBlock(painter, bounds, 0xFF000000);

	// Draw the sprite.
	EsRectangle spriteBounds = ES_RECT_4PD(spriteX, spriteY, 30, 30);
	EsDrawRectangle(painter, EsRectangleTranslate(spriteBounds, bounds), 0xFFFF0000, 0xFF800000, ES_RECT_1(2));
}

void GameUpdate(float deltaMs) {
	// Move the sprite if an arrow key is held.
	float directionX = isLeftHeld ? -1 : isRightHeld ? 1 : 0;
	spriteX += directionX * deltaMs * 0.25f;
	float directionY = isUpHeld ? -1 : isDownHeld ? 1 : 0;
	spriteY += directionY * deltaMs * 0.25f;
}

int CanvasMessage(EsElement *, EsMessage *message) {
	if (message->type == ES_MSG_PAINT) {
		// Get the bounds we should draw on.
		EsRectangle bounds = EsPainterBoundsInset(message->painter);

		// Render the game.
		GameRender(message->painter, bounds);
	} else if (message->type == ES_MSG_ANIMATE) {
		// Keep sending animation messages.
		message->animate.complete = false;

		// Update the game using the time delta (milliseconds) provided.
		GameUpdate(message->animate.deltaMs);

		// Ask to repaint the canvas.
		EsElementRepaint(canvas, nullptr);
	} else if (message->type == ES_MSG_KEY_DOWN || message->type == ES_MSG_KEY_UP) {
		// Track which keys are being held.
		if (message->keyboard.scancode == ES_SCANCODE_LEFT_ARROW) {
			isLeftHeld = message->type == ES_MSG_KEY_DOWN;
		} else if (message->keyboard.scancode == ES_SCANCODE_RIGHT_ARROW) {
			isRightHeld = message->type == ES_MSG_KEY_DOWN;
		} else if (message->keyboard.scancode == ES_SCANCODE_UP_ARROW) {
			isUpHeld = message->type == ES_MSG_KEY_DOWN;
		} else if (message->keyboard.scancode == ES_SCANCODE_DOWN_ARROW) {
			isDownHeld = message->type == ES_MSG_KEY_DOWN;
		}
	}

	return 0;
}

void _start() {
	_init();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			instance = EsInstanceCreate(message, "Game Loop");

			// Create a canvas to draw on, and make it fill the window.
			canvas = EsCustomElementCreate(instance->window, ES_CELL_FILL | ES_ELEMENT_FOCUSABLE, ES_STYLE_PANEL_WINDOW_DIVIDER);

			// Set the message callback.
			canvas->messageUser = CanvasMessage;

			// Send animation messages to the canvas.
			EsElementStartAnimating(canvas);

			// Focus the canvas to receive keyboard messages.
			EsElementFocus(canvas);
		}
	}
}
