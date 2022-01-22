/*
 * rgltf
 *
 * A raylib extension to load and draw glTF 2.0 models
 *
 * MIT License
 * Copyright (c) 2022 Roy Qu
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <raylib.h>

#if defined(__cplusplus)
extern "C" {            // Prevents name mangling of functions
#endif

// glTF Model Node
typedef struct GLTFNode {
	struct GLTFNode *parent;   // Parent nodes;
	struct GLTFNode *children; // Children nodes array;
	// every primitive in the glTF as a separate raylib mesh, so we need to covert mesh id in glTF to interval
	int meshStart;                // Start position of the interval in the model mesh array;
	int meshEnd;                  // End position of the interval in the model mesh array;
	Transform transform;          // Transform for node meshes;
	Matrix transformMatrix;       // Transform matrix for node meshes;
} GLTFNode;

// Scene
typedef struct GLTFScene {
	int nodeCount;          // Number of nodes
	int *nodes;            // Scene nodes array
} GLTFScene;
// Model, meshes, materials

typedef struct GLTFModel {
	Matrix transform;       // Local transform matrix

	int meshCount;          // Number of meshes
	int materialCount;      // Number of materials
	Mesh *meshes;           // Meshes array
	Material *materials;    // Materials array
	int *meshMaterial;      // Mesh material number

	// Scene and node data
	int nodeCount;          // Number of nodes;
	GLTFNode *nodes;       // Nodes array;

	int sceneCount;         // Number of scenes
	GLTFScene *scenes;          // Scenes array
	int scene;              // Scene should be displayed
} GLTFModel;

RLAPI GLTFModel LoadGLTFModel(const char *fileName);	//Load GTLF model
RLAPI void UnloadGLTFModel(GLTFModel model);
RLAPI void DrawGLTFModel(GLTFModel model, Vector3 position, float scale, Color tint);                           // Draw a model (with texture if set)
RLAPI void DrawGLTFModelEx(GLTFModel model, Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint); // Draw a model with extended parameters
RLAPI void DrawGLTFModelWires(GLTFModel model, Vector3 position, float scale, Color tint);                      // Draw a model wires (with texture if set)
RLAPI void DrawGLTFModelWiresEx(GLTFModel model, Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint); // Draw a model wires (with texture if set) with extended parameters
RLAPI void DrawGLTFModelScene(GLTFModel model, int scene_id, Vector3 position, float scale, Color tint);        // Draw a model's scene (with texture if set)
RLAPI void DrawGLTFModelSceneEx(GLTFModel model, int scene_id, Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint); // Draw a model's scene with extended parameters
RLAPI void DrawGLTFModelSceneWires(GLTFModel model, int scene_id, Vector3 position, float scale, Color tint);   // Draw a model's scene wires (with texture if set)
RLAPI void DrawGLTFModelSceneWiresEx(GLTFModel model, int scene_id, Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint); // Draw a model's scene wires (with texture if set) with extended parameters

#if defined(__cplusplus)
}            // Prevents name mangling of functions
#endif

