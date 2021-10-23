// gcc -o tri tri.c -lOSMesa && ./tri

#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <essence.h>

#define MODERN_GL

#define CHECK_ERRORS() do { GLenum error; while ((error = glGetError()) != GL_NO_ERROR) { EsPrint("Error on line %d: %d\n", __LINE__, error); } } while (0)

#define IMAGE_WIDTH (700)
#define IMAGE_HEIGHT (600)
uint32_t *buffer;

uint32_t modelVBO, modelIBO;
bool loadedModel;

#ifdef MODERN_GL
static GLenum (*glCheckFramebufferStatus)(GLenum target);
static GLint (*glGetUniformLocation)(GLuint program, const GLchar *name);
static GLuint (*glCreateProgram)();
static GLuint (*glCreateShader)(GLenum type);
static void (*glAttachShader)(GLuint program, GLuint shader);
static void (*glBindBuffer)(GLenum target, GLuint buffer);
static void (*glBindFramebuffer)(GLenum target, GLuint framebuffer);
static void (*glBindVertexArray)(GLuint array);
static void (*glBufferData)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
static void (*glCompileShader)(GLuint shader);
static void (*glDeleteFramebuffers)(GLsizei n, const GLuint *framebuffers);
static void (*glDrawBuffers)(GLsizei n, const GLenum *bufs);
static void (*glEnableVertexAttribArray)();
static void (*glFramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
static void (*glGenBuffers)(GLsizei n, GLuint *buffers);
static void (*glGenFramebuffers)(GLsizei n, GLuint *framebuffers);
static void (*glGenVertexArrays)(GLsizei n, GLuint *arrays);
static void (*glGetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
static void (*glGetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
static void (*glGetShaderiv)(GLuint shader, GLenum pname, GLint *param);
static void (*glLinkProgram)(GLuint program);
static void (*glShaderSource)(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
static void (*glUniform1f)(GLint location, GLfloat v0);
static void (*glUniform1i)(GLint location, GLint v0);
static void (*glUniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
static void (*glUniformMatrix3fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
static void (*glUniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
static void (*glUseProgram)(GLuint program);
static void (*glValidateProgram)(GLuint program);
static void (*glVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);

#define GL_CLAMP_TO_EDGE 0x812F
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_MULTISAMPLE 0x809D
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_BGRA 0x80E1
#endif

uint32_t framesDrawn;
double lastTime;
float timeMs;
float previousTimeMs;
double lastGLDuration;
double lastDrawBitmapDuration;

int shaderTransform, shaderNormalTransform;
size_t triangleCount, vertexCount;

void Transform(float *left, float *right, float *output) {
	float result[16];
	
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			float s = left[0 + i * 4] * right[j + 0 * 4]
				+ left[1 + i * 4] * right[j + 1 * 4]
				+ left[2 + i * 4] * right[j + 2 * 4]
				+ left[3 + i * 4] * right[j + 3 * 4];
			result[i * 4 + j] = s;
		}
	}
	
	memcpy(output, result, sizeof(result));
}

void PrepareNormalTransform(float *_modelTransform, float *_normalTransform) {
	float modelTransform[4][4];
	float normalTransform[3][3];
	float determinant = 0;

	memcpy(modelTransform, _modelTransform, 4 * 4 * sizeof(float));

	for (uintptr_t i = 0; i < 3; i++) {
		determinant += modelTransform[0][i] * (modelTransform[1][(i + 1) % 3] * modelTransform[2][(i + 2) % 3]
				- modelTransform[1][(i + 2) % 3] * modelTransform[2][(i + 1) % 3]);
	}

	for (uintptr_t i = 0; i < 3; i++) {
		for (uintptr_t j = 0; j < 3; j++) {
			normalTransform[i][j] = ((modelTransform[(i + 1) % 3][(j + 1) % 3] * modelTransform[(i + 2) % 3][(j + 2) % 3]) 
					- (modelTransform[(i + 1) % 3][(j + 2) % 3] * modelTransform[(i + 2) % 3][(j + 1) % 3])) / determinant;
		}
	}

	memcpy(_normalTransform, normalTransform, 3 * 3 * sizeof(float));
}

void Render() {
	if (!loadedModel) {
		return;
	}

	if (previousTimeMs == timeMs) {
		return;
	}

	previousTimeMs = timeMs;
	EsPerformanceTimerPush();

	glClearColor(0.21f, 0.2f, 0.2f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#ifdef MODERN_GL
	float m = timeMs / 1000.0f;
	float transform[16] = { 0.9f, 0, 0, 0, /**/ 0, 1, 0, 0, /**/ 0, 0, 1, 0, /**/ 0, 0, 0, 1 };
	float normalTransform[9];
	float rotation[16] = { cosf(m), 0, sinf(m), 0, /**/ 0, 1, 0, 0, /**/ -sinf(m), 0, cosf(m), 0, /**/ 0, 0, 0, 1 };
	float rotation2[16] = { 1, 0, 0, 0, /**/ 0, cosf(0.3f), sinf(0.3f), 0, /**/ 0, -sinf(0.3f), cosf(0.3f), 0, /**/ 0, 0, 0, 1 };
	float final[16] = { 0.2f, 0, 0, 0, /**/ 0, -0.2f, 0, 0, /**/ 0, 0, 0.2f, 0, /**/ 0, 0.15f, 0.5f, 1 };
	Transform(rotation2, transform, transform);
	Transform(rotation, transform, transform);
	PrepareNormalTransform(transform, normalTransform);
	glUniformMatrix3fv(shaderNormalTransform, 1, GL_FALSE, normalTransform);
	Transform(final, transform, transform);
	glUniformMatrix4fv(shaderTransform, 1, GL_FALSE, transform);
	glDrawElements(GL_TRIANGLES, 3 * triangleCount, GL_UNSIGNED_INT, 0);
#else
	glLoadIdentity();
	glRotatef(fmodf(timeMs * 0.1f, 360.0f), 1, 0, 0);
	glBegin(GL_TRIANGLES);
	glColor4f(1, 0, 0, 1);
	glVertex2f(-0.5f, -0.5f);
	glColor4f(0, 1, 0, 1);
	glVertex2f(0, 0.5f);
	glColor4f(0, 0, 1, 1);
	glVertex2f(0.5f, -0.5f);
	glEnd();
#endif
	glFinish();

	framesDrawn++;
	lastGLDuration = EsPerformanceTimerPop();
}

int CanvasCallback(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_PAINT_BACKGROUND) {
		Render();
		EsPerformanceTimerPush();
		EsRectangle bounds = EsPainterBoundsInset(message->painter);
		EsRectangle imageBounds = EsRectangleCenter(bounds, ES_RECT_2S(IMAGE_WIDTH, IMAGE_HEIGHT));
		EsDrawBitmap(message->painter, imageBounds, buffer, IMAGE_WIDTH * 4, ES_DRAW_BITMAP_OPAQUE);
		EsDrawBlock(message->painter, ES_RECT_4(bounds.l, imageBounds.l, bounds.t, bounds.b), 0xFF333336);
		EsDrawBlock(message->painter, ES_RECT_4(imageBounds.r, bounds.r, bounds.t, bounds.b), 0xFF333336);
		EsDrawBlock(message->painter, ES_RECT_4(imageBounds.l, imageBounds.r, bounds.t, imageBounds.t), 0xFF333336);
		EsDrawBlock(message->painter, ES_RECT_4(imageBounds.l, imageBounds.r, imageBounds.b, bounds.b), 0xFF333336);
		lastDrawBitmapDuration = EsPerformanceTimerPop();
	} else if (message->type == ES_MSG_ANIMATE) {
		double currentTime = EsTimeStampMs();

		if (currentTime - lastTime > 1000.0) {
			EsPrint("%d fps (last frame: GL %F s, DrawBitmap %F s)\n", framesDrawn, lastGLDuration, lastDrawBitmapDuration);
			lastTime = currentTime;
			framesDrawn = 0;
		}

		message->animate.complete = false;
		timeMs += message->animate.deltaMs;
		
		EsRectangle imageBounds = EsRectangleCenter(EsElementGetInsetBounds(element), ES_RECT_2S(IMAGE_WIDTH, IMAGE_HEIGHT));
		EsElementRepaint(element, &imageBounds);
		return ES_HANDLED;
	}

	return 0;
}

bool LoadModel(char *model, size_t modelBytes) {
	triangleCount = 0, vertexCount = 0;

	for (uintptr_t i = 0; i < modelBytes; i++) {
		if (!i || model[i - 1] == '\n') {
			if (model[i] == 'f' && model[i + 1] == ' ') {
				triangleCount++;
			} else if (model[i] == 'v' && model[i + 1] == ' ') {
				vertexCount++;
			}
		}
	}

	float *modelVBOArray = (float *) EsHeapAllocate(6 * sizeof(float) * vertexCount, true, NULL);
	uint32_t *modelIBOArray = (uint32_t *) EsHeapAllocate(3 * sizeof(uint32_t) * triangleCount, true, NULL);
	uintptr_t triangleIndex = 0, vertexIndex = 0;

	float minimumX = ES_INFINITY, maximumX = -ES_INFINITY; 
	float minimumY = ES_INFINITY, maximumY = -ES_INFINITY;
	float minimumZ = ES_INFINITY, maximumZ = -ES_INFINITY;

	for (uintptr_t i = 0; i < modelBytes; i++) {
		if (!i || model[i - 1] == '\n') {
			if (model[i] == 'v' && model[i + 1] == ' ') {
				char *position = model + i + 2;
				modelVBOArray[6 * vertexIndex + 0] = strtod(position, &position);
				modelVBOArray[6 * vertexIndex + 1] = strtod(position, &position);
				modelVBOArray[6 * vertexIndex + 2] = strtod(position, &position);
				minimumX = modelVBOArray[6 * vertexIndex + 0] < minimumX ? modelVBOArray[6 * vertexIndex + 0] : minimumX;
				minimumY = modelVBOArray[6 * vertexIndex + 1] < minimumY ? modelVBOArray[6 * vertexIndex + 1] : minimumY;
				minimumZ = modelVBOArray[6 * vertexIndex + 2] < minimumZ ? modelVBOArray[6 * vertexIndex + 2] : minimumZ;
				maximumX = modelVBOArray[6 * vertexIndex + 0] > maximumX ? modelVBOArray[6 * vertexIndex + 0] : maximumX;
				maximumY = modelVBOArray[6 * vertexIndex + 1] > maximumY ? modelVBOArray[6 * vertexIndex + 1] : maximumY;
				maximumZ = modelVBOArray[6 * vertexIndex + 2] > maximumZ ? modelVBOArray[6 * vertexIndex + 2] : maximumZ;
				vertexIndex++;
			} else if (model[i] == 'f' && model[i + 1] == ' ') {
				char *position = model + i + 2;
				uint32_t i0 = strtoul(position, &position, 10) - 1;
				uint32_t i1 = strtoul(position, &position, 10) - 1;
				uint32_t i2 = strtoul(position, &position, 10) - 1;
				if (i0 >= vertexCount) return false;
				if (i1 >= vertexCount) return false;
				if (i2 >= vertexCount) return false;
				modelIBOArray[3 * triangleIndex + 0] = i0;
				modelIBOArray[3 * triangleIndex + 1] = i1;
				modelIBOArray[3 * triangleIndex + 2] = i2;
				triangleIndex++;
			}
		}
	}

	EsPrint("Model bounds: %F -> %F, %F -> %F, %F -> %F\n", minimumX, maximumX, minimumY, maximumY, minimumZ, maximumZ);
	EsAssert(vertexIndex == vertexCount);
	EsAssert(triangleIndex == triangleCount);

	for (uintptr_t i = 0; i < triangleCount; i++) {
		// Calculate the normals as a weighted average of the face normals, 
		// where the weight is the surface area of the face.

		float d1x = modelVBOArray[6 * modelIBOArray[3 * i + 1] + 0] - modelVBOArray[6 * modelIBOArray[3 * i + 0] + 0];
		float d1y = modelVBOArray[6 * modelIBOArray[3 * i + 1] + 1] - modelVBOArray[6 * modelIBOArray[3 * i + 0] + 1];
		float d1z = modelVBOArray[6 * modelIBOArray[3 * i + 1] + 2] - modelVBOArray[6 * modelIBOArray[3 * i + 0] + 2];
		float d2x = modelVBOArray[6 * modelIBOArray[3 * i + 2] + 0] - modelVBOArray[6 * modelIBOArray[3 * i + 0] + 0];
		float d2y = modelVBOArray[6 * modelIBOArray[3 * i + 2] + 1] - modelVBOArray[6 * modelIBOArray[3 * i + 0] + 1];
		float d2z = modelVBOArray[6 * modelIBOArray[3 * i + 2] + 2] - modelVBOArray[6 * modelIBOArray[3 * i + 0] + 2];
		float nx = d1y * d2z - d1z * d2y;
		float ny = d1z * d2x - d1x * d2z;
		float nz = d1x * d2y - d1y * d2x;

		for (uintptr_t j = 0; j < 3; j++) {
			modelVBOArray[6 * modelIBOArray[3 * i + j] + 3] += nx;
			modelVBOArray[6 * modelIBOArray[3 * i + j] + 4] += ny;
			modelVBOArray[6 * modelIBOArray[3 * i + j] + 5] += nz;
		}
	}

	for (uintptr_t i = 0; i < vertexCount; i++) {
		// Normalize the normals.

		float x = modelVBOArray[6 * i + 3];
		float y = modelVBOArray[6 * i + 4];
		float z = modelVBOArray[6 * i + 5];
		float d = sqrtf(x * x + y * y + z * z);
		modelVBOArray[6 * i + 3] /= d;
		modelVBOArray[6 * i + 4] /= d;
		modelVBOArray[6 * i + 5] /= d;
	}

	glBindBuffer(GL_ARRAY_BUFFER, modelVBO);
	glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(float) * vertexCount, modelVBOArray, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, modelIBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 3 * sizeof(uint32_t) * triangleCount, modelIBOArray, GL_STATIC_DRAW);

	EsHeapFree(modelVBOArray, 0, NULL);
	EsHeapFree(modelIBOArray, 0, NULL);

	return true;
}

int main(int argc, char **argv) {
	(void) argc;
	(void) argv;

#ifndef MODERN_GL
	OSMesaContext context = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, NULL);
	buffer = (uint32_t *) malloc(IMAGE_WIDTH * IMAGE_HEIGHT * 4);
	OSMesaMakeCurrent(context, buffer, GL_UNSIGNED_BYTE, IMAGE_WIDTH, IMAGE_HEIGHT);
#else
	const int contextAttributes[] = {
		OSMESA_FORMAT, OSMESA_RGBA,
		OSMESA_DEPTH_BITS, 16,
		OSMESA_PROFILE, OSMESA_CORE_PROFILE,
		OSMESA_CONTEXT_MAJOR_VERSION, 3,
		OSMESA_CONTEXT_MINOR_VERSION, 0,
		0
	};

	OSMesaContext context = OSMesaCreateContextAttribs(contextAttributes, NULL);
	buffer = (uint32_t *) malloc(IMAGE_WIDTH * IMAGE_HEIGHT * 4);
	OSMesaMakeCurrent(context, buffer, GL_UNSIGNED_BYTE, IMAGE_WIDTH, IMAGE_HEIGHT);

#define LOADEXT(x) *(void **) &x = (void *) OSMesaGetProcAddress(#x); assert(x);
	LOADEXT(glAttachShader);
	LOADEXT(glBindBuffer);
	LOADEXT(glBindFramebuffer);
	LOADEXT(glBindVertexArray);
	LOADEXT(glBufferData);
	LOADEXT(glCheckFramebufferStatus);
	LOADEXT(glCompileShader);
	LOADEXT(glCreateProgram);
	LOADEXT(glCreateShader);
	LOADEXT(glDeleteFramebuffers);
	LOADEXT(glDrawBuffers);
	LOADEXT(glEnableVertexAttribArray);
	LOADEXT(glFramebufferTexture2D);
	LOADEXT(glGenBuffers);	
	LOADEXT(glGenFramebuffers);
	LOADEXT(glGenVertexArrays);
	LOADEXT(glGetProgramInfoLog);
	LOADEXT(glGetShaderInfoLog);
	LOADEXT(glGetShaderiv);
	LOADEXT(glGetUniformLocation);
	LOADEXT(glLinkProgram);	
	LOADEXT(glShaderSource);
	LOADEXT(glUniform1f);
	LOADEXT(glUniform1i);
	LOADEXT(glUniform4f);
	LOADEXT(glUniformMatrix3fv);
	LOADEXT(glUniformMatrix4fv);
	LOADEXT(glUseProgram);
	LOADEXT(glValidateProgram);
	LOADEXT(glVertexAttribPointer);
#undef LOADEXT

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_MULTISAMPLE);

	glGenBuffers(1, &modelVBO);
	glGenBuffers(1, &modelIBO);

	const char *vertexShaderSource = 
		"#version 330\n"
		"layout(location = 0) in vec3 Position;\n"
		"layout(location = 1) in vec3 Normal;\n"
		"uniform mat4 transform;\n"
		"uniform mat3 normalTransform;\n"
		"out vec3 Normal0;\n"
		"void main() { \n"
		"	gl_Position = transform * vec4(Position, 1.0);\n"
		"	Normal0 = normalTransform * Normal;\n"
		"}\n";
	const char *fragmentShaderSource = 
		"#version 330\n"
		"layout(location = 0) out vec4 FragColor;\n"
		"in vec3 Normal0;\n"
		"void main() { \n"
		"	vec3 n = normalize(Normal0);\n"
		"	vec3 lightDirection = vec3(0, -0.707, 0.707);\n"
		"	vec3 color = vec3(1.0, 0.9, 0.9);\n"
		"	float lightFactor = max(0, -dot(n, lightDirection));\n"
		"	// FragColor = vec4(n.xyz, 1);\n" // Visualize normals.
		"	// FragColor = vec4(vec3(gl_FragCoord.z), 1);\n" // Visualize Z coordinates.
		"	FragColor = vec4(color * vec3(lightFactor), 1);\n"
		"}\n";

	const char *shaderSources[] = { vertexShaderSource, fragmentShaderSource };
	int shaderSourceLengths[] = { strlen(vertexShaderSource), strlen(fragmentShaderSource) };

	unsigned shader = glCreateProgram();
	char shaderInfoLog[1024];

	unsigned vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, shaderSources + 0, shaderSourceLengths + 0);
	glCompileShader(vertexShader);
	glGetShaderInfoLog(vertexShader, sizeof(shaderInfoLog), NULL, shaderInfoLog);
	glAttachShader(shader, vertexShader);	
	EsPrint("Vertex shader log: '%z'\n", shaderInfoLog);

	unsigned fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, shaderSources + 1, shaderSourceLengths + 1);
	glCompileShader(fragmentShader);
	glGetShaderInfoLog(fragmentShader, sizeof(shaderInfoLog), NULL, shaderInfoLog);
	glAttachShader(shader, fragmentShader);	
	EsPrint("Fragment shader log: '%z'\n", shaderInfoLog);

	glLinkProgram(shader);
	glValidateProgram(shader);

	shaderTransform = glGetUniformLocation(shader, "transform");
	shaderNormalTransform = glGetUniformLocation(shader, "normalTransform");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	unsigned vertexArray;
	glGenVertexArrays(1, &vertexArray);
	glBindVertexArray(vertexArray);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, modelVBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, modelIBO);
	glVertexAttribPointer(0 /* Position */, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (const GLvoid *) (0 * sizeof(float)));
	glVertexAttribPointer(1 /* Normal */, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (const GLvoid *) (3 * sizeof(float)));
	glUseProgram(shader);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
#endif
	
	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			EsInstance *instance = EsInstanceCreate(message, "Object Viewer", -1);
			EsWindowSetIcon(instance->window, ES_ICON_MODEL);
			EsElement *canvas = EsCustomElementCreate(instance->window, ES_CELL_FILL, 0);
			canvas->messageUser = (EsUICallback) CanvasCallback;
			EsElementStartAnimating(canvas);
		} else if (message->type == ES_MSG_INSTANCE_OPEN) {
			size_t modelBytes;
			void *model = EsFileStoreReadAll(message->instanceOpen.file, &modelBytes);
			EsInstanceOpenComplete(message, LoadModel(model, modelBytes), NULL, 0);
			EsHeapFree(model, 0, NULL);
			loadedModel = true;
		}
	}

	return 0;
}
