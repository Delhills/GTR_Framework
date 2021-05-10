#include "renderer.h"

#include "camera.h"
#include "shader.h"
#include "mesh.h"
#include "texture.h"
#include "prefab.h"
#include "material.h"
#include "utils.h"
#include "scene.h"
#include "extra/hdre.h"
#include <algorithm>

using namespace GTR;

Renderer::Renderer() {
	render_mode = eRenderMode::DEFAULT;
}

void Renderer::addRenderCalltoList(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera, float dist) {

	renderCall aux;

	aux.model = model;
	aux.mesh = mesh;
	aux.material = material;
	aux.camera = camera;
	aux.distance_to_cam = dist;

	this->renderCallList.push_back(aux);

}

void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{

	renderCallList.clear();

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	//render entities
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->entity_type == PREFAB)
		{
			PrefabEntity* pent = (GTR::PrefabEntity*)ent;
			if(pent->prefab)
				renderPrefab(ent->model, pent->prefab, camera);
		}
	}

	std::sort(renderCallList.begin(), renderCallList.end(), compareNodes);

	for (int i = 0; i < renderCallList.size(); i++) {
		renderMeshWithMaterial(renderCallList[i].model, renderCallList[i].mesh, renderCallList[i].material, renderCallList[i].camera);
	}
}

//renders all the prefab
void Renderer::renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
	assert(prefab && "PREFAB IS NULL");
	//assign the model to the root node
	renderNode(model, &prefab->root, camera);
}

//renders a node of the prefab and its children
void Renderer::renderNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);

		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		{
			//render node mesh
			//renderMeshWithMaterial( node_model, node->mesh, node->material, camera );
			addRenderCalltoList(node_model, node->mesh, node->material, camera, camera->eye.distance(world_bounding.center));
			//node->mesh->renderBounding(node_model, true);
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		renderNode(prefab_model, node->children[i], camera);
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;
	Texture* texture = NULL;
	GTR::Scene* scene = GTR::Scene::instance;

	texture = material->color_texture.texture;
	//texture = material->emissive_texture;
	//texture = material->metallic_roughness_texture.texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;
	if (texture == NULL)
		texture = Texture::getWhiteTexture(); //a 1x1 white texture

	Texture* metallic_roughness_texture = material->metallic_roughness_texture.texture;
	if (metallic_roughness_texture == NULL)
		metallic_roughness_texture = Texture::getWhiteTexture(); //a 1x1 white texture


	Texture* emmisive_texture = material->emissive_texture.texture;
	if (emmisive_texture == NULL)
		emmisive_texture = Texture::getWhiteTexture(); //a 1x1 white texture

	//select the blending
	if (material->alpha_mode == GTR::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

	//chose a shader

	if (render_mode == SHOW_NORMAL) {
		shader = Shader::Get("normal");
	}
	else if (render_mode == SHOW_TEXTURE) {
		shader = Shader::Get("texture");
	}
	else if (render_mode == SHOW_UVS) {
		shader = Shader::Get("uvs");
	}
	else if (render_mode == SHOW_OCCLUSION) {
		shader = Shader::Get("occlusion");
	}
	else if (render_mode == DEFAULT) {
		shader = Shader::Get("basiclight");
	}
	else if (render_mode == MULTI) {
		shader = Shader::Get("multipasslight");
	}

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model );
	float t = getTime();
	shader->setUniform("u_time", t );

	shader->setUniform("u_color", material->color);
	shader->setUniform("u_emissive_factor", material->emissive_factor);

	shader->setUniform("u_ambient_light", scene->ambient_light);

	std::vector<GTR::LightEntity*> lightsScene = scene->lights;



	if(texture)
		shader->setUniform("u_texture", texture, 0);
	if (metallic_roughness_texture)
		shader->setUniform("u_metallic_roughness_texture", metallic_roughness_texture, 1);
	if(emmisive_texture)
		shader->setUniform("u_emmisive_texture", emmisive_texture, 2);
	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	if (render_mode == DEFAULT) {
		for (size_t i = 0; i < lightsScene.size() && i < 5; i++) {
			light_types[i] = lightsScene[i]->light_type;
			light_color[i] = lightsScene[i]->color * lightsScene[i]->intensity;
			light_position[i] = lightsScene[i]->model.getTranslation();
			light_maxdists[i] = lightsScene[i]->max_distance;
			light_coscutoff[i] = cos((lightsScene[i]->cone_angle / 180.0) * PI);
			light_spotexponent[i] = lightsScene[i]->spot_exponent;
			light_target[i] = lightsScene[i]->model.frontVector();
			light_vector[i] = lightsScene[i]->target;
		}

		shader->setUniform1Array("u_light_type", (int*)&light_types, 3);
		shader->setUniform3Array("u_light_pos", (float*)&light_position, 3);
		shader->setUniform3Array("u_light_target", (float*)&light_target, 3);
		shader->setUniform3Array("u_light_color", (float*)&light_color, 3);
		shader->setUniform1Array("u_light_max_dists", (float*)&light_maxdists, 3);
		shader->setUniform1Array("u_light_coscutoff", (float*)&light_coscutoff, 3);
		shader->setUniform1Array("u_light_spotexp", (float*)&light_spotexponent, 3);
		shader->setUniform1("u_num_lights", 3);
		//do the draw call that renders the mesh into the screen
		mesh->render(GL_TRIANGLES);
	}

	else if (render_mode == MULTI) {
		for (size_t i = 0; i < lightsScene.size() && i < 5; i++) {
			shader->setUniform("u_light_type", lightsScene[i]->light_type);
			shader->setUniform("u_light_pos", lightsScene[i]->model.getTranslation());
			shader->setUniform("u_light_target", lightsScene[i]->target);
			shader->setUniform("u_light_color", lightsScene[i]->color * lightsScene[i]->intensity);
			shader->setUniform("u_light_max_dists", lightsScene[i]->max_distance);
			shader->setUniform("u_light_coscutoff", (float)cos((lightsScene[i]->cone_angle / 180.0) * PI));
			shader->setUniform("u_light_spotexp", lightsScene[i]->spot_exponent);
			if (i != 0) {
				shader->setUniform("u_ambient_light", Vector3(0.0, 0.0, 0.0));
				shader->setUniform("u_emissive_factor", Vector3(0.0, 0.0, 0.0));

				glDepthFunc(GL_LEQUAL);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);

				glEnable(GL_BLEND);
			}
			mesh->render(GL_TRIANGLES);
		}
		glDepthFunc(GL_LESS);
	}
	else {
		mesh->render(GL_TRIANGLES);
	}

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
}


Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = new HDRE();
	if (!hdre->load(filename))
	{
		delete hdre;
		return NULL;
	}

	/*
	Texture* texture = new Texture();
	texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFaces(0), hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT );
	for(int i = 1; i < 6; ++i)
		texture->uploadCubemap(texture->format, texture->type, false, (Uint8**)hdre->getFaces(i), GL_RGBA32F, i);
	return texture;
	*/
	return NULL;
}
