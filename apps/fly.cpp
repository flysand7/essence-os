// TODO Icon.
// TODO Music.
// TODO Draw() with rotation.
// TODO Saving with new file IO.

#include <essence.h>

EsInstance *instance;
uint32_t backgroundColor;
int musicIndex;
bool keysPressedSpace, keysPressedEscape, keysPressedX;
uint32_t previousControllerButtons[ES_GAME_CONTROLLER_MAX_COUNT];
uint32_t *targetBits;
size_t targetWidth, targetHeight, targetStride;
double updateTimeAccumulator;
float gameScale;
uint32_t gameOffsetX, gameOffsetY, gameWidth, gameHeight;

#define GAME_SIZE (380)
#define BRAND "fly"

struct Texture { 
	uint32_t width, height; 
	uint32_t *bits;
};

void Transform(float *destination, float *left, float *right) {
	float d[6];
	d[0] = left[0] * right[0] + left[1] * right[3];
	d[1] = left[0] * right[1] + left[1] * right[4];
	d[2] = left[0] * right[2] + left[1] * right[5] + left[2];
	d[3] = left[3] * right[0] + left[4] * right[3];
	d[4] = left[3] * right[1] + left[4] * right[4];
	d[5] = left[3] * right[2] + left[4] * right[5] + left[5];
	destination[0] = d[0];
	destination[1] = d[1];
	destination[2] = d[2];
	destination[3] = d[3];
	destination[4] = d[4];
	destination[5] = d[5];
}

ES_FUNCTION_OPTIMISE_O3 
void Draw(Texture *texture, 
		float x, float y, float w = -1, float h = -1, 
		float sx = 0, float sy = 0, float sw = -1, float sh = -1, 
		float _a = 1, float _r = 1, float _g = 1, float _b = 1,
		float rot = 0) {
	(void) rot;

	if (sw == -1 && sh == -1) sw = texture->width, sh = texture->height;
	if (w == -1 && h == -1) w = sw, h = sh;
	if (_a <= 0) return;
	
	if (x + w < 0 || y + h < 0 || x > GAME_SIZE || y > GAME_SIZE) return;

	x *= gameScale, y *= gameScale, w *= gameScale, h *= gameScale;

	float m[6] = {};
	float m1[6] = { sw / w, 0, 0, 0, sh / h, 0 };
	float m2[6] = { 1, 0, -x * (sw / w), 0, 1, -y * (sh / h) };

	Transform(m, m2, m1);

	intptr_t yStart = y - 1, yEnd = y + h + 1, xStart = x - 1, xEnd = x + w + 1;
	// if (rot) yStart -= h * 0.45f, yEnd += h * 0.45f, xStart -= w * 0.45f, xEnd += w * 0.45f;
	if (yStart < 0) yStart = 0; 
	if (yStart > gameHeight) yStart = gameHeight;
	if (yEnd < 0) yEnd = 0; 
	if (yEnd > gameHeight) yEnd = gameHeight;
	if (xStart < 0) xStart = 0; 
	if (xStart > gameWidth) xStart = gameWidth;
	if (xEnd < 0) xEnd = 0; 
	if (xEnd > gameWidth) xEnd = gameWidth;
	m[2] += m[0] * xStart, m[5] += m[3] * xStart;

	uint32_t *scanlineStart = targetBits + (gameOffsetY + yStart) * targetStride / 4 + (gameOffsetX + xStart);

	uint32_t r = _r * 255, g = _g * 255, b = _b * 255, a = _a * 255;

	for (intptr_t y = yStart; y < yEnd; y++, scanlineStart += targetStride / 4) {
		uint32_t *output = scanlineStart;

		float tx = m[1] * y + m[2];
		float ty = m[4] * y + m[5];

		for (intptr_t x = xStart; x < xEnd; x++, output++, tx += m[0], ty += m[3]) {
			if (tx + sx < 0 || ty + sy < 0) continue;
			uintptr_t txi = tx + sx, tyi = ty + sy;
			if (txi < sx || tyi < sy || txi >= sx + sw || tyi >= sy + sh) continue;
			uint32_t modified = texture->bits[tyi * texture->width + txi];
			if (!(modified & 0xFF000000)) continue;
			uint32_t original = *output;
			uint32_t alpha1 = (((modified & 0xFF000000) >> 24) * a) >> 8;
			uint32_t alpha2 = (255 - alpha1) << 8;
			uint32_t r2 = alpha2 * ((original & 0x00FF0000) >> 16);
			uint32_t g2 = alpha2 * ((original & 0x0000FF00) >> 8);
			uint32_t b2 = alpha2 * ((original & 0x000000FF) >> 0);
			uint32_t r1 = r * alpha1 * ((modified & 0x00FF0000) >> 16);
			uint32_t g1 = g * alpha1 * ((modified & 0x0000FF00) >> 8);
			uint32_t b1 = b * alpha1 * ((modified & 0x000000FF) >> 0);
			*output = 0xFF000000 
				| (0x00FF0000 & ((r1 + r2) << 0)) 
				| (0x0000FF00 & ((g1 + g2) >> 8)) 
				| (0x000000FF & ((b1 + b2) >> 16));
		}
	}
}

void CreateTexture(Texture *texture, const char *cName) {
	size_t dataBytes;
	const void *data = EsEmbeddedFileGet(cName, -1, &dataBytes);
	texture->bits = (uint32_t *) EsImageLoad(data, dataBytes, &texture->width, &texture->height, 4);
	EsAssert(texture->bits);
}

void ExitGame() {
	EsInstanceDestroy(instance);
}

///////////////////////////////////////////////////////////

#define TAG_ALL (-1)
#define TAG_SOLID (-2)
#define TAG_KILL (-3)
#define TAG_WORLD (-4)

#define TILE_SIZE (16)

struct Entity {
	uint8_t tag;
	uint8_t layer;
	bool solid, kill, hide;
	char symbol;
	int8_t frameCount, stepsPerFrame;
	Texture *texture;
	float texOffX, texOffY, w, h;
	int uid;
	
	void (*create)(Entity *);
	void (*destroy)(Entity *);
	void (*stepAlways)(Entity *); // Called even if level editing.
	void (*step)(Entity *);
	void (*draw)(Entity *);
	void (*drawAfter)(Entity *);
	
	float x, y, px, py;
	int stepIndex;
	bool isUsed, isDestroyed;
	
	union {
		struct {
			float cx, cy;
			float th, dth;
			float dths;
			int respawn, respawnGrow, dash;
		} player;
		
		struct {
			int random;
		} block;
		
		struct {
			float vel;
		} moveBlock;
		
		struct {
			float th, cx, cy;
		} spin;
		
		struct {
			float vx, vy, th, dth;
			int life;
			uint32_t col;
		} star;
	};
	
	void Destroy() {
		if (isDestroyed) return;
		isDestroyed = true;
		if (destroy) destroy(this);
	}
};

Texture textureDashMessage, textureKeyMessage, textureKey, textureControlsMessage, textureWhite;

Entity typePlayer, typeBlock, typeCheck, typeMoveH, typeStar, typeMoveV, typePowerup, typeShowMessage, typeKey, typeLock, typeSpin, typeEnd;

// All the entities that can be loaded from the rooms.
Entity *entityTypes[] = { 
	&typeBlock, &typeCheck, &typeMoveH, &typeMoveV, &typePowerup, &typeKey, &typeLock, &typeSpin, &typeEnd,
	nullptr,
};
	
int levelTick;
bool justLoaded = true;

#define MAX_ENTITIES (1000)

struct SaveState {
	float checkX, checkY;
	int roomX, roomY;
	bool hasDash, hasKey;
	int deathCount;
	uint32_t check;
};

struct GameState : SaveState {
	Entity entities[MAX_ENTITIES];
	int world;
	Entity *player;
};

GameState state;

Entity *AddEntity(Entity *templateEntity, float x, float y, int uid = 0) {
	for (int i = 0; i < MAX_ENTITIES; i++) {
		if (!state.entities[i].isUsed) {
			EsMemoryCopy(state.entities + i, templateEntity, sizeof(Entity));
			state.entities[i].isUsed = true;
			if (!state.entities[i].frameCount) state.entities[i].frameCount = 1;
			if (!state.entities[i].stepsPerFrame) state.entities[i].stepsPerFrame = 1;
			state.entities[i].x = state.entities[i].px = x;
			state.entities[i].y = state.entities[i].py = y;
			if (!state.entities[i].w) state.entities[i].w = state.entities[i].texture->width - 1;
			if (!state.entities[i].h) state.entities[i].h = state.entities[i].texture->height / state.entities[i].frameCount - 1;
			state.entities[i].uid = uid;
			if (state.entities[i].create) state.entities[i].create(state.entities + i);
			return state.entities + i;
		}
	}
	
	static Entity fake = {};
	return &fake;
}
	
Entity *FindEntity(float x, float y, float w, float h, int tag, Entity *exclude) {
	for (int i = 0; i < MAX_ENTITIES; i++) {
		if (state.entities[i].isUsed && !state.entities[i].isDestroyed
				&& (state.entities[i].tag == tag 
					|| tag == TAG_ALL 
					|| (tag == TAG_SOLID && state.entities[i].solid)
					|| (tag == TAG_KILL && state.entities[i].kill)
					)
				&& state.entities + i != exclude) {
			if (x <= state.entities[i].x + state.entities[i].w && state.entities[i].x <= x + w 
					&& y <= state.entities[i].y + state.entities[i].h && state.entities[i].y <= y + h) {
				return state.entities + i;
			}
		}
	}
	
	return nullptr;
}

char roomName[16];

void UpdateRoomName() {
	roomName[0] = 'R';
	roomName[1] = (state.roomX / 10) + '0';
	roomName[2] = (state.roomX % 10) + '0';
	roomName[3] = '_';
	roomName[4] = (state.roomY / 10) + '0';
	roomName[5] = (state.roomY % 10) + '0';
	roomName[6] = '.';
	roomName[7] = 'D';
	roomName[8] = 'A';
	roomName[9] = 'T';
	roomName[10] = 0;
}

#define ROOM_ID ((state.roomX - 7) + (state.roomY - 9) * 6)

void LoadRoom() {
	state.world = 0;
	
	UpdateRoomName();
	
	roomName[6] = '_';
	const uint8_t *buffer = (const uint8_t *) EsEmbeddedFileGet(roomName, -1);
	
	for (int i = 0; i < MAX_ENTITIES; i++) {
		if (!state.entities[i].isUsed || state.entities[i].isDestroyed) continue;
		
		if (state.entities[i].tag != typePlayer.tag) {
			state.entities[i].Destroy();
		} else {
			state.entities[i].px = state.entities[i].x;
			state.entities[i].py = state.entities[i].y;
		}
	}
	
	int p = 0;
	int iir = 0;
	
	while (true) {
		uint8_t tag = buffer[p++];
		if (!tag) break;
		float x = *(float *) (buffer + p); p += 4;
		float y = *(float *) (buffer + p); p += 4;
		
		if (tag == (uint8_t) TAG_WORLD) {
			state.world = x;
		}
		
		for (int i = 0; entityTypes[i]; i++) {
			if (entityTypes[i]->tag == tag) {
				AddEntity(entityTypes[i], x, y, (state.roomX << 24) | (state.roomY << 16) | iir);
				iir++;
			}
		}
	}
	
	musicIndex = state.world;
}

void CalculateCheck() {
	state.check = 0;
	
	uint8_t *buffer = (uint8_t *) &state;
	uint32_t check = 0x1D471D47;
	
	for (uintptr_t i = 0; i < sizeof(SaveState); i++) {
		check ^= ((uint32_t) buffer[i] + 10) * (i + 100);
	}
	
	state.check = check;
}

float FadeInOut(float t) {
	if (t < 0.3f) return t / 0.3f;
	else if (t < 0.7f) return 1;
	else return 1 - (t - 0.7f) / 0.3f;
}

void InitialiseGame() {
	state.roomX = 10;
	state.roomY = 10;
	
	CreateTexture(&textureWhite, "white_png");
	CreateTexture(&textureKey, "key_png");
	CreateTexture(&textureDashMessage, "dash_msg_png");
	CreateTexture(&textureKeyMessage, "key_msg_png");
	CreateTexture(&textureControlsMessage, "controls_png");
	
	int tag = 1;
	
	{
		static Texture texture;
		CreateTexture(&texture, "player_png");
		typePlayer.tag = tag++;
		typePlayer.texture = &texture;
		typePlayer.frameCount = 6;
		typePlayer.stepsPerFrame = 5;
		typePlayer.layer = 1;
		
		typePlayer.step = [] (Entity *entity) {
			if (entity->player.respawn) {
				if (entity->stepIndex > entity->player.respawn) {
					entity->player.respawn = 0;
					entity->hide = false;
					entity->player.cx = state.checkX;
					entity->player.cy = state.checkY;
					entity->player.respawnGrow = entity->stepIndex + 20;
					entity->player.dash = 0;
				} else {
					return;
				}
			}
			
			if (!entity->player.respawnGrow && !entity->player.dash) {
				if (keysPressedSpace) {
					entity->player.cx += (entity->x - entity->player.cx) * 2;
					entity->player.cy += (entity->y - entity->player.cy) * 2;
					entity->player.th += 3.15f;
					entity->player.dth = -entity->player.dth;
					entity->player.dths = 5;
				} else if (keysPressedX && state.hasDash) {
					entity->player.dash = 10;
				}
			}
			
			float rd = 40;
			
			if (entity->player.respawnGrow) {
				if (entity->stepIndex > entity->player.respawnGrow) {
					entity->player.respawnGrow = 0;
				} else {
					rd *= 1 - (entity->player.respawnGrow - entity->stepIndex) / 20.0f;
				}
			}
			
			entity->x = rd * EsCRTcosf(entity->player.th) + entity->player.cx;
			entity->y = rd * EsCRTsinf(entity->player.th) + entity->player.cy + 5 * EsCRTsinf(4.71f + entity->stepIndex * 0.1f);
			
			if (entity->player.dash) {
				float pt = entity->player.th - entity->player.dth;
				float px = rd * EsCRTcosf(pt) + entity->player.cx;
				float py = rd * EsCRTsinf(pt) + entity->player.cy + 5 * EsCRTsinf(4.71f + (entity->stepIndex - 1) * 0.1f);
				float dx = entity->x - px;
				float dy = entity->y - py;
				entity->player.cx += dx * 3.2f;
				entity->player.cy += dy * 3.2f;
				entity->player.dash--;
				AddEntity(&typeStar, entity->x + 8, entity->y + 8)->star.col = 0xFF;
			} else {
				entity->player.th += entity->player.dth;
			}
			
			if (entity->player.respawnGrow) {
				entity->px = entity->x;
				entity->py = entity->y;
			}
			
			if (entity->player.dths) {
				entity->player.th += entity->player.dth;
				entity->player.dths--;
				entity->stepIndex = 5 * 3;
			}
			
			if (FindEntity(entity->x + 5, entity->y + 5, entity->w - 10, entity->h - 10, TAG_KILL, 0)) {
				for (int i = 0; i < 20; i++) {
					AddEntity(&typeStar, entity->x + 8, entity->y + 8)->star.col = 0xFF0000;
				}
				
				entity->hide = true;
				entity->player.respawn = entity->stepIndex + 20;
				state.deathCount++;
			}
			
			if (entity->x > GAME_SIZE - 8) {
				entity->x -= GAME_SIZE;
				entity->player.cx -= GAME_SIZE;
				state.checkX -= GAME_SIZE;
				state.roomX++;
				LoadRoom();
			} else if (entity->x < -8) {
				entity->x += GAME_SIZE;
				entity->player.cx += GAME_SIZE;
				state.checkX += GAME_SIZE;
				state.roomX--;
				LoadRoom();
			}
			
			if (entity->y > GAME_SIZE - 8) {
				entity->y -= GAME_SIZE;
				entity->player.cy -= GAME_SIZE;
				state.checkY -= GAME_SIZE;
				state.roomY++;
				LoadRoom();
			} else if (entity->y < -8) {
				entity->y += GAME_SIZE;
				entity->player.cy += GAME_SIZE;
				state.checkY += GAME_SIZE;
				state.roomY--;
				LoadRoom();
			}
		};
		
		typePlayer.create = [] (Entity *entity) { 
			state.player = entity; 
			entity->player.cx = entity->x;
			entity->player.cy = entity->y;
			entity->player.dth = 0.06f;
		};
	}
	
	{
		static Texture texture;
		CreateTexture(&texture, "block_png");
		
		static Texture block2;
		CreateTexture(&block2, "block2_png");
		
		typeBlock.tag = tag++;
		typeBlock.texture = &texture;
		typeBlock.kill = true;
		typeBlock.hide = true;
		typeBlock.layer = 2;
		
		typeBlock.create = [] (Entity *entity) {
			uint8_t r = EsRandomU8();
			
			if (r < 20) {
				entity->block.random = 1;
			} else if (r < 50) {
				entity->block.random = 2;
			} else {
				entity->block.random = 0;
			}
		};
		
		typeBlock.stepAlways = [] (Entity *entity) {
			if (FindEntity(entity->x + 1, entity->y + 1, entity->w - 2, entity->h - 2, entity->tag, entity)) {
				entity->Destroy();
			}
		};
		
		typeBlock.drawAfter = [] (Entity *entity) {
			if (!FindEntity(entity->x - 16, entity->y, 1, 1, entity->tag, entity)) {
				Draw(&block2, entity->x - 16, entity->y, 16, 16, 0, entity->block.random * 16, 16, 16, 1);
			}
			
			if (!FindEntity(entity->x + 16, entity->y, 1, 1, entity->tag, entity)) {
				Draw(&block2, entity->x + 16, entity->y, 16, 16, 32, entity->block.random * 16, 16, 16, 1);
			}
			
			if (!FindEntity(entity->x, entity->y - 16, 1, 1, entity->tag, entity)) {
				Draw(&block2, entity->x, entity->y - 16, 16, 16, 16, entity->block.random * 16, 16, 16, 1);
			}
			
			if (!FindEntity(entity->x, entity->y + 16, 1, 1, entity->tag, entity)) {
				Draw(&block2, entity->x, entity->y + 16, 16, 16, 48, entity->block.random * 16, 16, 16, 1);
			}
		};
	}
	
	{
		static Texture check1, check2;
		CreateTexture(&check1, "check1_png");
		CreateTexture(&check2, "check2_png");
		typeCheck.tag = tag++;
		typeCheck.texture = &check1;
		
		typeCheck.step = [] (Entity *entity) {
			if (state.checkX == entity->x && state.checkY == entity->y) {
				entity->texture = &check2;
			} else {
				entity->texture = &check1;
			}
			
			if (FindEntity(entity->x - 4, entity->y - 4, entity->w + 8, entity->h + 8, typePlayer.tag, 0)) {
				if (state.checkX != entity->x || state.checkY != entity->y) {
					for (int i = 0; i < 10; i++) AddEntity(&typeStar, entity->x + 8, entity->y + 8)->star.col = 0xFFFFFF;
				}
				
				state.checkX = entity->x;
				state.checkY = entity->y;
				
				CalculateCheck();
				EsFileWriteAll("|Settings:/Save.dat", -1, &state, sizeof(SaveState)); 
			}
		};
	}
	
	{
		static Texture texture;
		CreateTexture(&texture, "moveblock_png");
		typeMoveH.tag = tag++;
		typeMoveH.texture = &texture;
		typeMoveH.kill = true;
		typeMoveH.w = 16;
		typeMoveH.h = 16;
		typeMoveH.texOffX = -4;
		typeMoveH.texOffY = -4;
		
		typeMoveH.create = [] (Entity *entity) {
			entity->moveBlock.vel = -4;
		};
		
		typeMoveH.step = [] (Entity *entity) {
			entity->x += entity->moveBlock.vel;
			
			if (FindEntity(entity->x, entity->y, entity->w, entity->h, typeBlock.tag, 0)) {
				entity->moveBlock.vel = -entity->moveBlock.vel;
			}
		};
	}
	
	{
		// Removed entity.
		tag++;
	}
	
	{
		static Texture texture;
		CreateTexture(&texture, "star_png");
		typeStar.texture = &texture;
		typeStar.tag = tag++;
		typeStar.hide = true; // Draw manually.
		typeStar.layer = 2;
		
		typeStar.create = [] (Entity *entity) {
			float th = EsRandomU8() / 255.0 * 6.24;
			float sp = EsRandomU8() / 255.0 * 0.5 + 0.5;
			entity->star.vx = sp * EsCRTcosf(th);
			entity->star.vy = sp * EsCRTsinf(th);
			entity->star.life = EsRandomU8();
			entity->star.dth = (EsRandomU8() / 255.0f - 0.5f) * 0.2f;
		};
		
		typeStar.step = [] (Entity *entity) {
			entity->x += entity->star.vx;
			entity->y += entity->star.vy;
			entity->star.th += entity->star.dth;
			
			if (entity->star.life < entity->stepIndex) {
				entity->Destroy();
			}
		};
		
		typeStar.drawAfter = [] (Entity *entity) {
			Draw(entity->texture, entity->x - 4, entity->y - 4, -1, -1, 0, 0, -1, -1, 
				1.0 - (float) entity->stepIndex / entity->star.life, 
				((entity->star.col >> 16) & 0xFF) / 255.0f,
				((entity->star.col >> 8) & 0xFF) / 255.0f,
				((entity->star.col >> 0) & 0xFF) / 255.0f,
				entity->star.th);
		};
	}
	
	{
		static Texture texture;
		CreateTexture(&texture, "moveblock_png");
		typeMoveV.tag = tag++;
		typeMoveV.texture = &texture;
		typeMoveV.kill = true;
		typeMoveV.w = 16;
		typeMoveV.h = 16;
		typeMoveV.texOffX = -4;
		typeMoveV.texOffY = -4;
		
		typeMoveV.create = [] (Entity *entity) {
			entity->moveBlock.vel = -4;
		};
		
		typeMoveV.step = [] (Entity *entity) {
			entity->y += entity->moveBlock.vel;
			
			if (FindEntity(entity->x, entity->y, entity->w, entity->h, typeBlock.tag, 0)) {
				entity->moveBlock.vel = -entity->moveBlock.vel;
			}
		};
	}
	
	{
		static Texture texture;
		CreateTexture(&texture, "powerup_png");
		typePowerup.tag = tag++;
		typePowerup.texture = &texture;
		
		typePowerup.step = [] (Entity *entity) {
			if (state.hasDash) {
				entity->Destroy();
				return;
			}
			
			if (FindEntity(entity->x, entity->y, entity->w, entity->h, typePlayer.tag, 0)) {
				state.hasDash = true;
				AddEntity(&typeShowMessage, 0, 0)->texture = &textureDashMessage;
				entity->Destroy();
			}
		};
	}
	
	{
		typeShowMessage.texture = &textureKeyMessage;
		typeShowMessage.tag = tag++;
		typeShowMessage.hide = true;
		
		typeShowMessage.draw = [] (Entity *entity) {
			Draw(entity->texture, entity->x, entity->y, -1, -1, 0, 0, -1, -1, FadeInOut(entity->stepIndex / 180.0));
			if (entity->stepIndex > 180) entity->Destroy();
		};
	}
	
	{
		typeKey.tag = tag++;
		typeKey.texture = &textureKey;
		
		typeKey.step = [] (Entity *entity) {
			if (state.hasKey) {
				entity->Destroy();
			} else if (FindEntity(entity->x, entity->y, entity->w, entity->h, typePlayer.tag, 0)) {
				state.hasKey = true;
				AddEntity(&typeShowMessage, 0, 0)->texture = &textureKeyMessage;
				entity->Destroy();
				for (int i = 0; i < 10; i++) AddEntity(&typeStar, entity->x + 8, entity->y + 8)->star.col = 0xFFFFFF;
			}
		};
	}
	
	{
		static Texture texture;
		CreateTexture(&texture, "lock_png");
		typeLock.tag = tag++;
		typeLock.texture = &texture;
		typeLock.kill = true;
		
		typeLock.step = [] (Entity *entity) {
			if (state.hasKey) {
				for (int i = 0; i < 1; i++) AddEntity(&typeStar, entity->x + 8, entity->y + 8)->star.col = 0x000000;
				entity->Destroy();
			}
		};
	}
	
	{
		static Texture texture;
		CreateTexture(&texture, "moveblock_png");
		typeSpin.tag = tag++;
		typeSpin.texture = &texture;
		typeSpin.kill = true;
		typeSpin.w = 16;
		typeSpin.h = 16;
		typeSpin.texOffX = -4;
		typeSpin.texOffY = -4;
		
		typeSpin.create = [] (Entity *entity) {
			entity->spin.cx = entity->x;
			entity->spin.cy = entity->y;
		};
		
		typeSpin.step = [] (Entity *entity) {
			entity->x = 60 * EsCRTcosf(entity->spin.th) + entity->spin.cx;
			entity->y = 60 * EsCRTsinf(entity->spin.th) + entity->spin.cy;
			entity->spin.th += 0.04f;
		};
	}
	
	{
		static Texture msg1, msg2, msg3, num;
		CreateTexture(&msg1, "end1_png");
		CreateTexture(&msg2, "end2_png");
		CreateTexture(&msg3, "end3_png");
		CreateTexture(&num, "numbers_png");
		
		typeEnd.tag = tag++;
		typeEnd.texture = &textureKey;
		typeEnd.hide = true;
		
		typeEnd.create = [] (Entity *) {
			state.player->Destroy();
		};
		
		typeEnd.draw = [] (Entity *entity) {
			float t = entity->stepIndex / 180.0f;
			
			if (t < 1) {
				Draw(&msg1, 40, 150, -1, -1, 0, 0, -1, -1, FadeInOut(t));
			} else if (t < 2) {
				Draw(&msg2, 40, 150, -1, -1, 0, 0, -1, -1, FadeInOut(t - 1));
			} else if (t < 3) {
				Draw(&msg3, 40, 150, -1, -1, 0, 0, -1, -1, FadeInOut(t - 2));
				
				int p = state.deathCount;
				char digits[10];
				int dc = 0;
				
				if (p == 0) {
					digits[dc++] = 0;
				} else {
					while (p) {
						digits[dc++] = p % 10;
						p /= 10;
					}
				}
				
				int w = dc * 16;
				
				for (int i = dc - 1; i >= 0; i--) {
					Draw(&num, 40 + 150 - w / 2 + (dc - 1 - i) * 16, 
						150 + 33, 16, 30, 16 * digits[i], 0, 16, 30, FadeInOut(t - 2));
				}
			} else {
				ExitGame();
			}
		};
	}
	
	state.checkX = GAME_SIZE / 2;
	state.checkY = GAME_SIZE / 2 - 20;

	size_t loadedStateBytes;
	SaveState *loadedState = (SaveState *) EsFileReadAll("|Settings:/Save.dat", -1, &loadedStateBytes);
	bool noSave = true;
	
	if (loadedStateBytes == sizeof(SaveState)) {
		EsMemoryCopy(&state, loadedState, loadedStateBytes);

		uint32_t oldCheck = state.check;
		CalculateCheck();
		EsAssert(oldCheck == state.check);
		
		noSave = false;
	}
	
	LoadRoom();
	
	AddEntity(&typePlayer, state.checkX, state.checkY);

	if (noSave) {
		AddEntity(&typeShowMessage, 0, GAME_SIZE - 65)->texture = &textureControlsMessage;
	}
}

void UpdateGame() {	
	if (keysPressedEscape) {
		ExitGame();
	}
	
	for (int i = 0; i < MAX_ENTITIES; i++) {
		if (state.entities[i].isUsed) {
			state.entities[i].stepIndex++;
			
			state.entities[i].px += (state.entities[i].x - state.entities[i].px) * 0.5f;
			state.entities[i].py += (state.entities[i].y - state.entities[i].py) * 0.5f;
		}
	}
	
	for (int i = 0; i < MAX_ENTITIES; i++) if (state.entities[i].isUsed && state.entities[i].stepAlways) state.entities[i].stepAlways(state.entities + i);
	
	for (int i = 0; i < MAX_ENTITIES; i++) if (state.entities[i].isUsed && state.entities[i].step) {
		state.entities[i].step(state.entities + i);
	}
	
	for (int i = 0; i < MAX_ENTITIES; i++) {
		if (state.entities[i].isUsed && state.entities[i].isDestroyed) state.entities[i].isUsed = false;
	}
	
	levelTick++;
	
	state.world %= 3;
	
	if (state.world == 0) {
		backgroundColor = 0xbef1b1;
	} else if (state.world == 1) {
		backgroundColor = 0xcee5f1;
	} else if (state.world == 2) {
		backgroundColor = 0xf3bdf6;
	}
}

void RenderGame() {	
	for (int layer = -1; layer <= 3; layer++) {
		for (int i = 0; i < MAX_ENTITIES; i++) {
			Entity *entity = state.entities + i;
			if (!entity->isUsed) continue;
			if (entity->layer != layer) continue;
			if (!entity->texture) continue;
			if (entity->hide) continue;
			
			int frame = entity->stepsPerFrame >= 0 ? ((entity->stepIndex / entity->stepsPerFrame) % entity->frameCount) : (-entity->stepsPerFrame - 1);
			Draw(entity->texture, (int) (entity->px + entity->texOffX + 0.5f), 
				(int) (entity->py + entity->texOffY + 0.5f),
				entity->texture->width, entity->texture->height / entity->frameCount,
				0, entity->texture->height / entity->frameCount * frame, 
				entity->texture->width, entity->texture->height / entity->frameCount);
		}
	}
	
	for (int i = 0; i < MAX_ENTITIES; i++) if (state.entities[i].isUsed && state.entities[i].draw      ) state.entities[i].draw      (state.entities + i);
	for (int i = 0; i < MAX_ENTITIES; i++) if (state.entities[i].isUsed && state.entities[i].drawAfter ) state.entities[i].drawAfter (state.entities + i);
	
	if (state.hasKey) {
		Draw(&textureKey, GAME_SIZE - 20, 4);
	}
}

///////////////////////////////////////////////////////////

int ProcessCanvasMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_ANIMATE) {
		message->animate.complete = false;
		updateTimeAccumulator += message->animate.deltaMs / 1000.0;

		while (updateTimeAccumulator > 1 / 60.0) {
			{
				EsGameControllerState state[ES_GAME_CONTROLLER_MAX_COUNT];
				size_t count = EsGameControllerStatePoll(state);

				for (uintptr_t i = 0; i < count; i++) {
					if (state[i].buttons & (1 << 0) && (~previousControllerButtons[i] & (1 << 0))) {
						keysPressedSpace = true;
					} else if (state[i].buttons & (1 << 1) && (~previousControllerButtons[i] & (1 << 1))) {
						keysPressedX = true;
					}

					previousControllerButtons[i] = state[i].buttons;
				}
			}

			UpdateGame();
			updateTimeAccumulator -= 1 / 60.0;
			keysPressedSpace = keysPressedEscape = keysPressedX = false;
		}

		EsElementRepaint(element);
	} else if (message->type == ES_MSG_KEY_DOWN && !message->keyboard.repeat) {
		if (message->keyboard.scancode == ES_SCANCODE_SPACE || message->keyboard.scancode == ES_SCANCODE_Z) {
			keysPressedSpace = true;
		} else if (message->keyboard.scancode == ES_SCANCODE_ESCAPE) {
			keysPressedEscape = true;
		} else if (message->keyboard.scancode == ES_SCANCODE_X) {
			keysPressedX = true;
		}
	} else if (message->type == ES_MSG_PAINT) {
		EsPainter *painter = message->painter;
		EsPaintTargetStartDirectAccess(painter->target, &targetBits, nullptr, nullptr, &targetStride);
		targetBits = (uint32_t *) ((uint8_t *) targetBits + targetStride * painter->offsetY + 4 * painter->offsetX);
		targetWidth = painter->width, targetHeight = painter->height;

		gameScale = (float) painter->width / GAME_SIZE;
		if (gameScale * GAME_SIZE > painter->height) gameScale = (float) painter->height / GAME_SIZE;
		if (gameScale > 1) gameScale = EsCRTfloorf(gameScale);
		gameWidth = GAME_SIZE * gameScale, gameHeight = GAME_SIZE * gameScale;
		gameOffsetX = painter->width / 2 - gameWidth / 2;
		gameOffsetY = painter->height / 2 - gameHeight / 2;

		// TODO Clear margins.

		Draw(&textureWhite, 0, 0, GAME_SIZE, GAME_SIZE, 0, 0, 1, 1, 1, 
				((backgroundColor >> 16) & 0xFF) / 255.0f, 
				((backgroundColor >> 8) & 0xFF) / 255.0f, 
				((backgroundColor >> 0) & 0xFF) / 255.0f);

		RenderGame();

		EsPaintTargetEndDirectAccess(painter->target);
	}

	return 0;
}

void _start() {
	_init();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			instance = EsInstanceCreate(message, BRAND);
			EsWindow *window = instance->window;
			EsPanel *container = EsPanelCreate(window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);
			EsElement *canvas = EsCustomElementCreate(container, ES_CELL_FILL | ES_ELEMENT_FOCUSABLE, {});
			canvas->messageUser = ProcessCanvasMessage;
			EsElementStartAnimating(canvas);
			EsElementFocus(canvas);
			InitialiseGame();
		}
	}
}
