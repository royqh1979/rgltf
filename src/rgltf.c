/*
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
#include "config.h"
#include "rgltf.h"
#include "cgltf.h"
#include <raymath.h>
#include <rlgl.h>
#include <stdlib.h>
#include <string.h>

#define TRACELOG(level, ...) TraceLog(level, __VA_ARGS__)

static Matrix TransformToMatrix(Transform transform) {
	Matrix matTranslation = MatrixTranslate(transform.translation.x,
										 transform.translation.y,
										 transform.translation.z);
	Matrix matRotation = QuaternionToMatrix(transform.rotation);
	Matrix matScale = MatrixScale(transform.scale.x,
							   transform.scale.y,
							   transform.scale.z);
	return MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);
}

static Image LoadImageFromCgltfImage(cgltf_image *cgltfImage, const char *texPath)
{
	Image image = { 0 };

	if (cgltfImage->uri != NULL)     // Check if image data is provided as a uri (base64 or path)
	{
		if ((strlen(cgltfImage->uri) > 5) &&
			(cgltfImage->uri[0] == 'd') &&
			(cgltfImage->uri[1] == 'a') &&
			(cgltfImage->uri[2] == 't') &&
			(cgltfImage->uri[3] == 'a') &&
			(cgltfImage->uri[4] == ':'))     // Check if image is provided as base64 text data
		{
			// Data URI Format: data:<mediatype>;base64,<data>

			// Find the comma
			int i = 0;
			while ((cgltfImage->uri[i] != ',') && (cgltfImage->uri[i] != 0)) i++;

			if (cgltfImage->uri[i] == 0) TRACELOG(LOG_WARNING, "IMAGE: glTF data URI is not a valid image");
			else
			{
				int base64Size = (int)strlen(cgltfImage->uri + i + 1);
				int outSize = 3*(base64Size/4);         // TODO: Consider padding (-numberOfPaddingCharacters)
				void *data = NULL;

				cgltf_options options = { 0 };
				cgltf_result result = cgltf_load_buffer_base64(&options, outSize, cgltfImage->uri + i + 1, &data);

				if (result == cgltf_result_success)
				{
					image = LoadImageFromMemory(".png", (unsigned char *)data, outSize);
					cgltf_free((cgltf_data*)data);
				}
			}
		}
		else     // Check if image is provided as image path
		{
			image = LoadImage(TextFormat("%s/%s", texPath, cgltfImage->uri));
		}
	}
	else if (cgltfImage->buffer_view->buffer->data != NULL)    // Check if image is provided as data buffer
	{
		unsigned char *data = RL_MALLOC(cgltfImage->buffer_view->size);
		int offset = (int)cgltfImage->buffer_view->offset;
		int stride = (int)cgltfImage->buffer_view->stride? (int)cgltfImage->buffer_view->stride : 1;

		// Copy buffer data to memory for loading
		for (unsigned int i = 0; i < cgltfImage->buffer_view->size; i++)
		{
			data[i] = ((unsigned char *)cgltfImage->buffer_view->buffer->data)[offset];
			offset += stride;
		}

		// Check mime_type for image: (cgltfImage->mime_type == "image/png")
		// NOTE: Detected that some models define mime_type as "image\\/png"
		if ((strcmp(cgltfImage->mime_type, "image\\/png") == 0) ||
			(strcmp(cgltfImage->mime_type, "image/png") == 0)) image = LoadImageFromMemory(".png", data, (int)cgltfImage->buffer_view->size);
		else if ((strcmp(cgltfImage->mime_type, "image\\/jpeg") == 0) ||
				 (strcmp(cgltfImage->mime_type, "image/jpeg") == 0)) image = LoadImageFromMemory(".jpg", data, (int)cgltfImage->buffer_view->size);
		else TRACELOG(LOG_WARNING, "MODEL: glTF image data MIME type not recognized", TextFormat("%s/%s", texPath, cgltfImage->uri));

		RL_FREE(data);
	}

	return image;
}

// Macro to simplify attributes loading code
#define LOAD_ATTRIBUTE(accesor, numComp, dataType, dstPtr) \
    { \
        int n = 0; \
        dataType *buffer = (dataType *)accesor->buffer_view->buffer->data + accesor->buffer_view->offset/sizeof(dataType) + accesor->offset/sizeof(dataType); \
        for (unsigned int k = 0; k < accesor->count; k++) \
        {\
            for (int l = 0; l < numComp; l++) \
            {\
                dstPtr[numComp*k + l] = buffer[n + l];\
            }\
            n += (int)(accesor->stride/sizeof(dataType));\
        }\
    }

static GLTFModel LoadGLTFModelFile(const char *fileName)
{
	GLTFModel model = { 0 };
	int *mesh_id_starts = NULL;
	int *mesh_id_ends = NULL;

	// glTF file loading
	unsigned int dataSize = 0;
	unsigned char *fileData = LoadFileData(fileName, &dataSize);

	if (fileData == NULL) return model;

	// glTF data loading
	cgltf_options options = { 0 };
	cgltf_data *data = NULL;
	cgltf_result result = cgltf_parse(&options, fileData, dataSize, &data);

	if (result == cgltf_result_success)
	{
		if (data->file_type == cgltf_file_type_glb) TRACELOG(LOG_INFO, "MODEL: [%s] Model basic data (glb) loaded successfully", fileName);
		else if (data->file_type == cgltf_file_type_gltf) TRACELOG(LOG_INFO, "MODEL: [%s] Model basic data (glTF) loaded successfully", fileName);
		else TRACELOG(LOG_WARNING, "MODEL: [%s] Model format not recognized", fileName);

		TRACELOG(LOG_INFO, "    > Meshes count: %i", data->meshes_count);
		TRACELOG(LOG_INFO, "    > Materials count: %i (+1 default)", data->materials_count);
		TRACELOG(LOG_DEBUG, "    > Buffers count: %i", data->buffers_count);
		TRACELOG(LOG_DEBUG, "    > Images count: %i", data->images_count);
		TRACELOG(LOG_DEBUG, "    > Textures count: %i", data->textures_count);
		TRACELOG(LOG_DEBUG, "    > Nodes count: %i", data->nodes_count);
		TRACELOG(LOG_DEBUG, "    > Scenes count: %i", data->scenes_count);

		// Force reading data buffers (fills buffer_view->buffer->data)
		// NOTE: If an uri is defined to base64 data or external path, it's automatically loaded -> TODO: Verify this assumption
		result = cgltf_load_buffers(&options, data, fileName);
		if (result != cgltf_result_success) TRACELOG(LOG_INFO, "MODEL: [%s] Failed to load mesh/material buffers", fileName);

        for (int i=0;i<data->nodes_count;i++) {
            TRACELOG(LOG_DEBUG, "node mesh %d %s", data->nodes[i].mesh, data->nodes[i].name);
        }

		mesh_id_starts = RL_CALLOC(data->meshes_count,sizeof(int));
		mesh_id_ends = RL_CALLOC(data->meshes_count,sizeof(int));

		int primitivesCount = 0;
		// NOTE: We will load every primitive in the glTF as a separate raylib mesh
		for (unsigned int i = 0; i < data->meshes_count; i++) primitivesCount += (int)data->meshes[i].primitives_count;

		// Load our pModel data: meshes and materials
		model.meshCount = primitivesCount;
		model.meshes = RL_CALLOC(model.meshCount, sizeof(Mesh));
		for (int i = 0; i < model.meshCount; i++) model.meshes[i].vboId = (unsigned int*)RL_CALLOC(MAX_MESH_VERTEX_BUFFERS, sizeof(unsigned int));

		// NOTE: We keep an extra slot for default material, in case some mesh requires it
		model.materialCount = (int)data->materials_count + 1;
		model.materials = RL_CALLOC(model.materialCount, sizeof(Material));
		model.materials[0] = LoadMaterialDefault();     // Load default material (index: 0)

		// Load mesh-material indices, by default all meshes are mapped to material index: 0
		model.meshMaterial = RL_CALLOC(model.meshCount, sizeof(int));

		// Load materials data
		//----------------------------------------------------------------------------------------------------
		for (unsigned int i = 0, j = 1; i < data->materials_count; i++, j++)
		{
			model.materials[j] = LoadMaterialDefault();
			const char *texPath = GetDirectoryPath(fileName);

			// Check glTF material flow: PBR metallic/roughness flow
			// NOTE: Alternatively, materials can follow PBR specular/glossiness flow
			if (data->materials[i].has_pbr_metallic_roughness)
			{
				// Load base color texture (albedo)
				if (data->materials[i].pbr_metallic_roughness.base_color_texture.texture)
				{
					Image imAlbedo = LoadImageFromCgltfImage(data->materials[i].pbr_metallic_roughness.base_color_texture.texture->image, texPath);
					if (imAlbedo.data != NULL)
					{
						model.materials[j].maps[MATERIAL_MAP_ALBEDO].texture = LoadTextureFromImage(imAlbedo);
						UnloadImage(imAlbedo);
					}
				}
				// Load base color factor (tint)
				model.materials[j].maps[MATERIAL_MAP_ALBEDO].color.r = (unsigned char)(data->materials[i].pbr_metallic_roughness.base_color_factor[0]*255);
				model.materials[j].maps[MATERIAL_MAP_ALBEDO].color.g = (unsigned char)(data->materials[i].pbr_metallic_roughness.base_color_factor[1]*255);
				model.materials[j].maps[MATERIAL_MAP_ALBEDO].color.b = (unsigned char)(data->materials[i].pbr_metallic_roughness.base_color_factor[2]*255);
				model.materials[j].maps[MATERIAL_MAP_ALBEDO].color.a = (unsigned char)(data->materials[i].pbr_metallic_roughness.base_color_factor[3]*255);

				// Load metallic/roughness texture
				if (data->materials[i].pbr_metallic_roughness.metallic_roughness_texture.texture)
				{
					Image imMetallicRoughness = LoadImageFromCgltfImage(data->materials[i].pbr_metallic_roughness.metallic_roughness_texture.texture->image, texPath);
					if (imMetallicRoughness.data != NULL)
					{
						model.materials[j].maps[MATERIAL_MAP_ROUGHNESS].texture = LoadTextureFromImage(imMetallicRoughness);
						UnloadImage(imMetallicRoughness);
					}

					// Load metallic/roughness material properties
					float roughness = data->materials[i].pbr_metallic_roughness.roughness_factor;
					model.materials[j].maps[MATERIAL_MAP_ROUGHNESS].value = roughness;

					float metallic = data->materials[i].pbr_metallic_roughness.metallic_factor;
					model.materials[j].maps[MATERIAL_MAP_METALNESS].value = metallic;
				}

				// Load normal texture
				if (data->materials[i].normal_texture.texture)
				{
					Image imNormal = LoadImageFromCgltfImage(data->materials[i].normal_texture.texture->image, texPath);
					if (imNormal.data != NULL)
					{
						model.materials[j].maps[MATERIAL_MAP_NORMAL].texture = LoadTextureFromImage(imNormal);
						UnloadImage(imNormal);
					}
				}

				// Load ambient occlusion texture
				if (data->materials[i].occlusion_texture.texture)
				{
					Image imOcclusion = LoadImageFromCgltfImage(data->materials[i].occlusion_texture.texture->image, texPath);
					if (imOcclusion.data != NULL)
					{
						model.materials[j].maps[MATERIAL_MAP_OCCLUSION].texture = LoadTextureFromImage(imOcclusion);
						UnloadImage(imOcclusion);
					}
				}

				// Load emissive texture
				if (data->materials[i].emissive_texture.texture)
				{
					Image imEmissive = LoadImageFromCgltfImage(data->materials[i].emissive_texture.texture->image, texPath);
					if (imEmissive.data != NULL)
					{
						model.materials[j].maps[MATERIAL_MAP_EMISSION].texture = LoadTextureFromImage(imEmissive);
						UnloadImage(imEmissive);
					}

					// Load emissive color factor
					model.materials[j].maps[MATERIAL_MAP_EMISSION].color.r = (unsigned char)(data->materials[i].emissive_factor[0]*255);
					model.materials[j].maps[MATERIAL_MAP_EMISSION].color.g = (unsigned char)(data->materials[i].emissive_factor[1]*255);
					model.materials[j].maps[MATERIAL_MAP_EMISSION].color.b = (unsigned char)(data->materials[i].emissive_factor[2]*255);
					model.materials[j].maps[MATERIAL_MAP_EMISSION].color.a = 255;
				}
			}

			// Other possible materials not supported by raylib pipeline:
			// has_clearcoat, has_transmission, has_volume, has_ior, has specular, has_sheen
		}

        TRACELOG(LOG_DEBUG,"%x",data->meshes);

		// Load meshes data
		//----------------------------------------------------------------------------------------------------
		for (unsigned int i = 0, meshIndex = 0; i < data->meshes_count; i++)
		{
			// NOTE: meshIndex accumulates primitives
			mesh_id_starts[i]=meshIndex;
			for (unsigned int p = 0; p < data->meshes[i].primitives_count; p++)
			{
				// NOTE: We only support primitives defined by triangles
				// Other alternatives: points, lines, line_strip, triangle_strip
				if (data->meshes[i].primitives[p].type != cgltf_primitive_type_triangles) continue;

				// NOTE: Attributes data could be provided in several data formats (8, 8u, 16u, 32...),
				// Only some formats for each attribute type are supported, read info at the top of this function!

				for (unsigned int j = 0; j < data->meshes[i].primitives[p].attributes_count; j++)
				{
					// Check the different attributes for every pimitive
					if (data->meshes[i].primitives[p].attributes[j].type == cgltf_attribute_type_position)      // POSITION
					{
						cgltf_accessor *attribute = data->meshes[i].primitives[p].attributes[j].data;

						// WARNING: SPECS: POSITION accessor MUST have its min and max properties defined.

						if ((attribute->component_type == cgltf_component_type_r_32f) && (attribute->type == cgltf_type_vec3))
						{
							// Init raylib mesh vertices to copy glTF attribute data
							model.meshes[meshIndex].vertexCount = (int)attribute->count;
							model.meshes[meshIndex].vertices = RL_MALLOC(attribute->count*3*sizeof(float));

							// Load 3 components of float data type into mesh.vertices
							LOAD_ATTRIBUTE(attribute, 3, float, model.meshes[meshIndex].vertices)
						}
						else TRACELOG(LOG_WARNING, "MODEL: [%s] Vertices attribute data format not supported, use vec3 float", fileName);
					}
					else if (data->meshes[i].primitives[p].attributes[j].type == cgltf_attribute_type_normal)   // NORMAL
					{
						cgltf_accessor *attribute = data->meshes[i].primitives[p].attributes[j].data;

						if ((attribute->component_type == cgltf_component_type_r_32f) && (attribute->type == cgltf_type_vec3))
						{
							// Init raylib mesh normals to copy glTF attribute data
							model.meshes[meshIndex].normals = RL_MALLOC(attribute->count*3*sizeof(float));

							// Load 3 components of float data type into mesh.normals
							LOAD_ATTRIBUTE(attribute, 3, float, model.meshes[meshIndex].normals)
						}
						else TRACELOG(LOG_WARNING, "MODEL: [%s] Normal attribute data format not supported, use vec3 float", fileName);
					}
					else if (data->meshes[i].primitives[p].attributes[j].type == cgltf_attribute_type_tangent)   // TANGENT
					{
						cgltf_accessor *attribute = data->meshes[i].primitives[p].attributes[j].data;

						if ((attribute->component_type == cgltf_component_type_r_32f) && (attribute->type == cgltf_type_vec4))
						{
							// Init raylib mesh tangent to copy glTF attribute data
							model.meshes[meshIndex].tangents = RL_MALLOC(attribute->count*4*sizeof(float));

							// Load 4 components of float data type into mesh.tangents
							LOAD_ATTRIBUTE(attribute, 4, float, model.meshes[meshIndex].tangents)
						}
						else TRACELOG(LOG_WARNING, "MODEL: [%s] Tangent attribute data format not supported, use vec4 float", fileName);
					}
					else if (data->meshes[i].primitives[p].attributes[j].type == cgltf_attribute_type_texcoord) // TEXCOORD_0
					{
						// TODO: Support additional texture coordinates: TEXCOORD_1 -> mesh.texcoords2

						cgltf_accessor *attribute = data->meshes[i].primitives[p].attributes[j].data;

						if ((attribute->component_type == cgltf_component_type_r_32f) && (attribute->type == cgltf_type_vec2))
						{
							// Init raylib mesh texcoords to copy glTF attribute data
							model.meshes[meshIndex].texcoords = RL_MALLOC(attribute->count*2*sizeof(float));

							// Load 3 components of float data type into mesh.texcoords
							LOAD_ATTRIBUTE(attribute, 2, float, model.meshes[meshIndex].texcoords)
						}
						else TRACELOG(LOG_WARNING, "MODEL: [%s] Texcoords attribute data format not supported, use vec2 float", fileName);
					}
					else if (data->meshes[i].primitives[p].attributes[j].type == cgltf_attribute_type_color)    // COLOR_0
					{
						cgltf_accessor *attribute = data->meshes[i].primitives[p].attributes[j].data;

						// WARNING: SPECS: All components of each COLOR_n accessor element MUST be clamped to [0.0, 1.0] range.

						if ((attribute->component_type == cgltf_component_type_r_8u) && (attribute->type == cgltf_type_vec4))
						{
							// Init raylib mesh color to copy glTF attribute data
							model.meshes[meshIndex].colors = RL_MALLOC(attribute->count*4*sizeof(unsigned char));

							// Load 4 components of unsigned char data type into mesh.colors
							LOAD_ATTRIBUTE(attribute, 4, unsigned char, model.meshes[meshIndex].colors)
						}
						else if ((attribute->component_type == cgltf_component_type_r_16u) && (attribute->type == cgltf_type_vec4))
						{
							// Init raylib mesh color to copy glTF attribute data
							model.meshes[meshIndex].colors = RL_MALLOC(attribute->count*4*sizeof(unsigned char));

							// Load data into a temp buffer to be converted to raylib data type
							unsigned short *temp = RL_MALLOC(attribute->count*4*sizeof(unsigned short));
							LOAD_ATTRIBUTE(attribute, 4, unsigned short, temp);

							// Convert data to raylib color data type (4 bytes)
							for (int c = 0; c < attribute->count*4; c++) model.meshes[meshIndex].colors[c] = (unsigned char)(((float)temp[c]/65535.0f)*255.0f);

							RL_FREE(temp);
						}
						else if ((attribute->component_type == cgltf_component_type_r_32f) && (attribute->type == cgltf_type_vec4))
						{
							// Init raylib mesh color to copy glTF attribute data
							model.meshes[meshIndex].colors = RL_MALLOC(attribute->count*4*sizeof(unsigned char));

							// Load data into a temp buffer to be converted to raylib data type
							float *temp = RL_MALLOC(attribute->count*4*sizeof(float));
							LOAD_ATTRIBUTE(attribute, 4, float, temp);

							// Convert data to raylib color data type (4 bytes), we expect the color data normalized
							for (int c = 0; c < attribute->count*4; c++) model.meshes[meshIndex].colors[c] = (unsigned char)(temp[c]*255.0f);

							RL_FREE(temp);
						}
						else TRACELOG(LOG_WARNING, "MODEL: [%s] Color attribute data format not supported", fileName);
					}

					// NOTE: Attributes related to animations are processed separately
				}

				// Load primitive indices data (if provided)
				if (data->meshes[i].primitives[p].indices != NULL)
				{
					cgltf_accessor *attribute = data->meshes[i].primitives[p].indices;

					model.meshes[meshIndex].triangleCount = (int)attribute->count/3;

					if (attribute->component_type == cgltf_component_type_r_16u)
					{
						// Init raylib mesh indices to copy glTF attribute data
						model.meshes[meshIndex].indices = RL_MALLOC(attribute->count*sizeof(unsigned short));

						// Load unsigned short data type into mesh.indices
						LOAD_ATTRIBUTE(attribute, 1, unsigned short, model.meshes[meshIndex].indices)
					}
					else if (attribute->component_type == cgltf_component_type_r_32u)
					{
						// Init raylib mesh indices to copy glTF attribute data
						model.meshes[meshIndex].indices = RL_MALLOC(attribute->count*sizeof(unsigned short));

						// Load data into a temp buffer to be converted to raylib data type
						unsigned int *temp = RL_MALLOC(attribute->count*sizeof(unsigned int));
						LOAD_ATTRIBUTE(attribute, 1, unsigned int, temp);

						// Convert data to raylib indices data type (unsigned short)
						for (int d = 0; d < attribute->count; d++) model.meshes[meshIndex].indices[d] = (unsigned short)temp[d];

						TRACELOG(LOG_WARNING, "MODEL: [%s] Indices data converted from u32 to u16, possible loss of data", fileName);

						RL_FREE(temp);
					}
					else TRACELOG(LOG_WARNING, "MODEL: [%s] Indices data format not supported, use u16", fileName);
				}
				else model.meshes[meshIndex].triangleCount = model.meshes[meshIndex].vertexCount/3;    // Unindexed mesh

				// Assign to the primitive mesh the corresponding material index
				// NOTE: If no material defined, mesh uses the already assigned default material (index: 0)
				for (int m = 0; m < data->materials_count; m++)
				{
					// The primitive actually keeps the pointer to the corresponding material,
					// raylib instead assigns to the mesh the by its index, as loaded in pModel.materials array
					// To get the index, we check if material pointers match and we assign the corresponding index,
					// skipping index 0, the default material
					if (&data->materials[m] == data->meshes[i].primitives[p].material)
					{
						model.meshMaterial[meshIndex] = m + 1;
						break;
					}
				}
//				if (pModel.meshMaterial[meshIndex]<0 || pModel.meshMaterial[meshIndex]>=pModel.materialCount)
//					pModel.meshMaterial[meshIndex] = 0;

				meshIndex++;       // Move to next mesh
			}
			mesh_id_ends[i]=meshIndex;
		}
        TRACELOG(LOG_DEBUG,"%x", data->meshes);

		// Load node data
		model.nodeCount = data->nodes_count;
		model.nodes = RL_CALLOC(data->nodes_count, sizeof(GLTFModel));
        TRACELOG(LOG_DEBUG,"Loading nodes %d", model.nodeCount);
		memset(model.nodes,0,data->nodes_count * sizeof(GLTFModel));
        TRACELOG(LOG_DEBUG,"-----------", model.nodeCount);
		for (unsigned int i = 0; i < data->nodes_count; i++) {
            if (data->nodes[i].mesh == NULL) {
                model.nodes[i].meshStart = -1;
                model.nodes[i].meshEnd = -1;
            } else {
                int mesh_id = (data->nodes[i].mesh - data->meshes);
                TRACELOG(LOG_DEBUG, "Mesh: %x %x %d %d", data->nodes[i].mesh, data->meshes, mesh_id,
                         data->meshes_count);
                model.nodes[i].meshStart = mesh_id_starts[mesh_id];
                model.nodes[i].meshEnd = mesh_id_ends[mesh_id];
            }

            model.nodes[i].childrenCount = data->nodes[i].children_count;
            model.nodes[i].children = RL_CALLOC(model.nodes[i].childrenCount,sizeof(int));
            for (unsigned  int j = 0; j < data->nodes[i].children_count; j++)
                model.nodes[i].children[j]=data->nodes[i].children[j]-data->nodes;
            Matrix matScale;
			Matrix matRotation;
			Matrix matTranslation;

			if (data->nodes[i].has_translation) {
				model.nodes[i].transform.translation.x = data->nodes[i].translation[0];
				model.nodes[i].transform.translation.y = data->nodes[i].translation[1];
				model.nodes[i].transform.translation.z = data->nodes[i].translation[2];
				matTranslation = MatrixTranslate(model.nodes[i].transform.translation.x,
												 model.nodes[i].transform.translation.y,
												 model.nodes[i].transform.translation.z);
			} else
				matTranslation = MatrixIdentity();
			if (data->nodes[i].has_rotation) {
				model.nodes[i].transform.rotation.x = data->nodes[i].rotation[0];
				model.nodes[i].transform.rotation.y = data->nodes[i].rotation[1];
				model.nodes[i].transform.rotation.z = data->nodes[i].rotation[2];
				model.nodes[i].transform.rotation.w = data->nodes[i].rotation[3];
				matRotation = QuaternionToMatrix(model.nodes[i].transform.rotation);
			} else
				matRotation = MatrixIdentity();

			if (data->nodes[i].has_scale) {
				model.nodes[i].transform.scale.x = data->nodes[i].scale[0];
				model.nodes[i].transform.scale.y = data->nodes[i].scale[1];
				model.nodes[i].transform.scale.z = data->nodes[i].scale[2];
				matScale = MatrixScale(model.nodes[i].transform.scale.x,
									   model.nodes[i].transform.scale.y,
									   model.nodes[i].transform.scale.z);
			} else {
				model.nodes[i].transform.scale.x = 1;
				model.nodes[i].transform.scale.y = 1;
				model.nodes[i].transform.scale.z = 1;
				matScale = MatrixIdentity();
			}
			model.nodes[i].transformMatrix = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);
		}
        TRACELOG(LOG_DEBUG,"Loading scene");

		// Load scene data
		if (data->scenes_count > 0) {
			model.scene = (data->scene - data->scenes);
			model.sceneCount = data->scenes_count;
			model.scenes = RL_CALLOC(data->scenes_count, sizeof(GLTFScene));
			memset(model.scenes, 0, sizeof(GLTFScene) * data->scenes_count);
			for (unsigned int i = 0; i < data->scenes_count; i++) {
				model.scenes[i].nodeCount = data->scenes[i].nodes_count;
				model.scenes[i].nodes = RL_CALLOC(data->scenes[i].nodes_count, sizeof(int));
				for (unsigned int j = 0; j < data->scenes[i].nodes_count; j++) {
					model.scenes[i].nodes[j] = (data->scenes[i].nodes[j] - data->nodes);
				}
			}
		}

/*
        // TODO: Load glTF meshes animation data
        // REF: https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#skins
        // REF: https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#skinned-mesh-attributes
        //----------------------------------------------------------------------------------------------------
        for (unsigned int i = 0, meshIndex = 0; i < data->meshes_count; i++)
        {
            for (unsigned int p = 0; p < data->meshes[i].primitives_count; p++)
            {
                // NOTE: We only support primitives defined by triangles
                if (data->meshes[i].primitives[p].type != cgltf_primitive_type_triangles) continue;

                for (unsigned int j = 0; j < data->meshes[i].primitives[p].attributes_count; j++)
                {
                    // NOTE: JOINTS_1 + WEIGHT_1 will be used for +4 joints influencing a vertex -> Not supported by raylib

                    if (data->meshes[i].primitives[p].attributes[j].type == cgltf_attribute_type_joints)        // JOINTS_n (vec4: 4 bones max per vertex / u8, u16)
                    {
                        cgltf_accessor *attribute = data->meshes[i].primitives[p].attributes[j].data;

                        if ((attribute->component_type == cgltf_component_type_r_8u) && (attribute->type == cgltf_type_vec4))
                        {
                            // Init raylib mesh bone ids to copy glTF attribute data
                            pModel.meshes[meshIndex].boneIds = RL_CALLOC(pModel.meshes[meshIndex].vertexCount*4, sizeof(unsigned char));

                            // Load 4 components of unsigned char data type into mesh.boneIds
                            LOAD_ATTRIBUTE(attribute, 4, unsigned char, pModel.meshes[meshIndex].boneIds)
                        }
                        else TRACELOG(LOG_WARNING, "MODEL: [%s] Joint attribute data format not supported, use vec4 u8", fileName);
                    }
                    else if (data->meshes[i].primitives[p].attributes[j].type == cgltf_attribute_type_weights)  // WEIGHTS_n (vec4 / u8, u16, f32)
                    {
                        cgltf_accessor *attribute = data->meshes[i].primitives[p].attributes[j].data;

                        if ((attribute->component_type == cgltf_component_type_r_32f) && (attribute->type == cgltf_type_vec4))
                        {
                            // Init raylib mesh bone weight to copy glTF attribute data
                            pModel.meshes[meshIndex].boneWeights = RL_CALLOC(pModel.meshes[meshIndex].vertexCount*4, sizeof(float));

                            // Load 4 components of float data type into mesh.boneWeights
                            LOAD_ATTRIBUTE(attribute, 4, float, pModel.meshes[meshIndex].boneWeights)
                        }
                        else TRACELOG(LOG_WARNING, "MODEL: [%s] Joint weight attribute data format not supported, use vec4 float", fileName);
                    }
                }

                meshIndex++;       // Move to next mesh
            }
        }
*/
		RL_FREE(mesh_id_starts);
		RL_FREE(mesh_id_ends);
		// Free all cgltf loaded data
		cgltf_free(data);
	}
	else TRACELOG(LOG_WARNING, "MODEL: [%s] Failed to load glTF data", fileName);

	// WARNING: cgltf requires the file pointer available while reading data
	UnloadFileData(fileData);


	return model;

}

// Draw a pModel (with texture if set)
void DrawGLTFModel(GLTFModel model, Vector3 position, float scale, Color tint)
{
	Vector3 vScale = { scale, scale, scale };
	Vector3 rotationAxis = { 0.0f, 1.0f, 0.0f };

	DrawGLTFModelEx(model, position, rotationAxis, 0.0f, vScale, tint);
}

// Draw a pModel with extended parameters
void DrawGLTFModelEx(GLTFModel model, Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint)
{
	// Calculate transformation matrix from function parameters
	// Get transform matrix (rotation -> scale -> translation)
	Matrix matScale = MatrixScale(scale.x, scale.y, scale.z);
	Matrix matRotation = MatrixRotate(rotationAxis, rotationAngle*DEG2RAD);
	Matrix matTranslation = MatrixTranslate(position.x, position.y, position.z);

	Matrix matTransform = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);

	// Combine pModel transformation matrix (pModel.transform) with matrix generated by function parameters (matTransform)
	model.transform = MatrixMultiply(model.transform, matTransform);
	if (model.scene >= 0
		&& model.scene < model.sceneCount
		&& model.scenes[model.scene].nodeCount>0) { // we have a scene to draw
		DrawGLTFScene(model, model.scene, model.transform, tint);
	} else {
		for (int i = 0; i < model.meshCount; i++) {
			Color color = model.materials[model.meshMaterial[i]].maps[MATERIAL_MAP_DIFFUSE].color;
			Color colorTint = WHITE;
			colorTint.r = (unsigned char) ((((float) color.r / 255.0) * ((float) tint.r / 255.0)) * 255.0f);
			colorTint.g = (unsigned char) ((((float) color.g / 255.0) * ((float) tint.g / 255.0)) * 255.0f);
			colorTint.b = (unsigned char) ((((float) color.b / 255.0) * ((float) tint.b / 255.0)) * 255.0f);
			colorTint.a = (unsigned char) ((((float) color.a / 255.0) * ((float) tint.a / 255.0)) * 255.0f);

			model.materials[model.meshMaterial[i]].maps[MATERIAL_MAP_DIFFUSE].color = colorTint;
			DrawMesh(model.meshes[i], model.materials[model.meshMaterial[i]], model.transform);
			model.materials[model.meshMaterial[i]].maps[MATERIAL_MAP_DIFFUSE].color = color;
		}
	}
}

// Draw a pModel wires (with texture if set)
void DrawGLTFModelWires(GLTFModel model, Vector3 position, float scale, Color tint)
{
	rlEnableWireMode();

	DrawGLTFModel(model, position, scale, tint);

	rlDisableWireMode();
}

// Draw a pModel wires (with texture if set) with extended parameters
void DrawGLTFModelWiresEx(GLTFModel model, Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint)
{
	rlEnableWireMode();

	DrawGLTFModelEx(model, position, rotationAxis, rotationAngle, scale, tint);

	rlDisableWireMode();
}

// Draw a Model's scene (with texture if set)
void DrawGLTFNode(GLTFModel model,int node_id, Matrix matTransform, Color tint) {
    if (node_id >= 0
        && node_id < model.nodeCount) {
        GLTFNode node = model.nodes[node_id];
        Matrix nodeTransform = MatrixMultiply(node.transformMatrix, matTransform);
        if (node.childrenCount>0) {
            for (int i = 0; i < node.childrenCount; i++){
                DrawGLTFNode(model,node.children[i],nodeTransform,tint);
            }
        } else {
            for (int j = node.meshStart; j < node.meshEnd; j++) {
                Color color = model.materials[model.meshMaterial[j]].maps[MATERIAL_MAP_DIFFUSE].color;
                Color colorTint = WHITE;
                colorTint.r = (unsigned char) ((((float) color.r / 255.0) * ((float) tint.r / 255.0)) * 255.0f);
                colorTint.g = (unsigned char) ((((float) color.g / 255.0) * ((float) tint.g / 255.0)) * 255.0f);
                colorTint.b = (unsigned char) ((((float) color.b / 255.0) * ((float) tint.b / 255.0)) * 255.0f);
                colorTint.a = (unsigned char) ((((float) color.a / 255.0) * ((float) tint.a / 255.0)) * 255.0f);

                model.materials[model.meshMaterial[j]].maps[MATERIAL_MAP_DIFFUSE].color = colorTint;
                DrawMesh(model.meshes[j], model.materials[model.meshMaterial[j]], nodeTransform);
                model.materials[model.meshMaterial[j]].maps[MATERIAL_MAP_DIFFUSE].color = color;
            }
        }
    }
}

// Draw a Model's scene wires (with texture if set)
void DrawGLTFNodeWires(GLTFModel model, int node_id, Matrix matTransform, Color tint)
{
    rlEnableWireMode();

    DrawGLTFNode(model, node_id, matTransform, tint);

    rlDisableWireMode();
}

// Draw a Model's scene with extended parameters
void DrawGLTFScene(GLTFModel model, int scene_id, Matrix matTransform, Color tint)
{
	if (scene_id >= 0
		&& scene_id < model.sceneCount) {
		for (int i = 0; i < model.scenes[scene_id].nodeCount; i++) {
			int node_id = model.scenes[scene_id].nodes[i];
            DrawGLTFNode(model,node_id,matTransform,tint);
		}
	}
}

// Draw a Model's scene wires (with texture if set)
void DrawGLTFSceneWires(GLTFModel model, int scene_id, Matrix matTransform, Color tint)
{
	rlEnableWireMode();

	DrawGLTFScene(model, scene_id, matTransform, tint);

	rlDisableWireMode();
}

void UnloadGLTFModel(GLTFModel model)
{
	// Unload meshes
	for (int i = 0; i < model.meshCount; i++) UnloadMesh(model.meshes[i]);

	// Unload materials maps
	// NOTE: As the user could be sharing shaders and textures between models,
	// we don't unload the material but just free it's maps,
	// the user is responsible for freeing models shaders and textures
	for (int i = 0; i < model.materialCount; i++) RL_FREE(model.materials[i].maps);

	// Unload arrays
	RL_FREE(model.meshes);
	RL_FREE(model.materials);
	RL_FREE(model.meshMaterial);

	// Unload scenes and nodes
    for (int i = 0; i< model.nodeCount; i++) RL_FREE(model.nodes[i].children);
	RL_FREE(model.nodes);
	for (int i = 0; i < model.sceneCount; i++) RL_FREE(model.scenes[i].nodes);
	RL_FREE(model.scenes);

	TRACELOG(LOG_INFO, "MODEL: Unloaded pModel (and meshes) from RAM and VRAM");
}

/**
 * Load glTF 2.0 pModel
 * 		Function implemented by Wilhem Barbier(@wbrbr), with modifications by Tyler Bezera(@gamerfiend)
 * 		and Roy Qu(@royqh1979)
 *
 * 		FEATURES:
 * 		- Supports .gltf and .glb files
 * 		- Supports embedded (base64) or external textures
 * 		- Supports PBR metallic/roughness flow, loads material textures, values and colors
 * 		PBR specular/glossiness flow and extended texture flows not supported
 * 		- Supports multiple meshes per pModel (every primitives is loaded as a separate mesh)
 *
 * 		RESTRICTIONS:
 * 		- Only triangle meshes supported
 * 		- Vertex attibute types and formats supported:
 * 		> Vertices (position): vec3: float
 * 		> Normals: vec3: float
 * 		> Texcoords: vec2: float
 * 		> Colors: vec4: u8, u16, f32 (normalized)
 * 		> Indices: u16, u32 (truncated to u16)
 */
GLTFModel  LoadGLTFModel(const char* fileName) {
	GLTFModel  model = LoadGLTFModelFile(fileName);
	// Make sure pModel transform is set to identity matrix!
	model.transform = MatrixIdentity();

	if (model.meshCount == 0)
	{
		model.meshCount = 1;
		model.meshes = (Mesh *)RL_CALLOC(model.meshCount, sizeof(Mesh));
#if defined(SUPPORT_MESH_GENERATION)
		TRACELOG(LOG_WARNING, "MESH: [%s] Failed to load mesh data, default to cube mesh", fileName);
		model.meshes[0] = GenMeshCube(1.0f, 1.0f, 1.0f);
#else
		TRACELOG(LOG_WARNING, "MESH: [%s] Failed to load mesh data", fileName);
#endif
	}
	else
	{
		// Upload vertex data to GPU (static mesh)
		for (int i = 0; i < model.meshCount; i++) UploadMesh(&model.meshes[i], false);
	}

	if (model.materialCount == 0)
	{
		TRACELOG(LOG_WARNING, "MATERIAL: [%s] Failed to load material data, default to white material", fileName);

		model.materialCount = 1;
		model.materials = (Material *)RL_CALLOC(model.materialCount, sizeof(Material));
		model.materials[0] = LoadMaterialDefault();

		if (model.meshMaterial == NULL) model.meshMaterial = (int *)RL_CALLOC(model.meshCount, sizeof(int));
	}

	return model;

}


