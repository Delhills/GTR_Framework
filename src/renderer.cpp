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
#include "application.h"
#include <algorithm>

using namespace GTR;

Renderer::Renderer() {
	int w = Application::instance->window_width;
	int h = Application::instance->window_height;

	average_lum = 1.0;
	lum_white = 1.0;
	scale_tm = 1.0;

	render_mode = eRenderMode::DEFAULT;
	pipeline_mode = ePipelineMode::DEFERRED;
	fbo.create(w,
				h,
				1,
				GL_RGBA,
				GL_FLOAT,
				false);
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


void Renderer::render(GTR::Scene* scene, Camera* camera) {

	renderSceneShadowmaps(scene);

	if (show_fbo) {

		if (showCameraDirectional) renderToFbo(scene, scene->lights[3]);
		else renderToFbo(scene, scene->lights[0]);
		
	}
	else {
		//renderToFbo(scene, camera, &fbo);
		renderScene(scene, camera, pipeline_mode);
	}

}


void Renderer::renderForward(GTR::Scene* scene, std::vector <renderCall>& rendercalls, Camera* camera) {

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	for (size_t i = 0; i < rendercalls.size(); i++)
	{
		renderCall& rc = rendercalls[i];
		renderMeshWithMaterial(render_mode, scene, rc.model, rc.mesh, rc.material, rc.camera);
	}
}

void Renderer::renderDeferred(GTR::Scene* scene, std::vector <renderCall>& rendercalls, Camera* camera) {
	
	int w = Application::instance->window_width;
	int h = Application::instance->window_height;
	
	if (gbuffers_fbo.fbo_id == 0) {
		gbuffers_fbo.create(w, 
							h,
							3,
							GL_RGBA,
							GL_FLOAT);
	}
	gbuffers_fbo.bind();

	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	checkGLErrors();

	for (size_t i = 0; i < rendercalls.size(); i++)
	{
		renderCall& rc = rendercalls[i];
		renderMeshWithMaterial(eRenderMode::GBUFFERS, scene, rc.model, rc.mesh, rc.material, rc.camera);
	}
	
	gbuffers_fbo.unbind();


	if (!ao_buffer)
		ao_buffer = new Texture(w, h, GL_LUMINANCE, GL_UNSIGNED_BYTE);

	if (!ao_blur_buffer)
		ao_blur_buffer = new Texture(w, h, GL_LUMINANCE, GL_UNSIGNED_BYTE);

	ssao.apply(gbuffers_fbo.depth_texture, gbuffers_fbo.color_textures[1], camera, ao_buffer);
	ssao.blurTexture(ao_buffer, ao_blur_buffer);

	fbo.bind(); //textura final pre hdr

	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);


	Mesh* mesh;
	Shader* shader;

	Mesh* sphere = Mesh::Get("data/meshes/sphere.obj", true);
	Mesh* quad = Mesh::getQuad();



	bool first_iter;

	for (size_t i = 0; i < scene->lights.size(); i++)
	{
		LightEntity* light = scene->lights[i];
		first_iter = (i == 0);

		if (light->light_type == DIRECTIONAL)
		{
			shader = Shader::Get("deferred");
			shader->enable();
			mesh = quad;

		}
		else {
			shader = Shader::Get("deferred");//deferred_sphere pero no nos ha funcionado
			shader->enable();
			mesh = quad; //sphere pero no nos ha funcionado
			Matrix44 m;
			Vector3 centre = light->model.getTranslation();
			m.setTranslation(centre.x, centre.y, centre.z);
			m.scale(light->max_distance, light->max_distance, light->max_distance);
			//shader->setUniform("u_model", m);

		}
		
		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		Matrix44 inv_vp = camera->viewprojection_matrix;
		inv_vp.inverse();
		shader->setUniform("hdr", hdr);

		shader->setUniform("u_camera_position", camera->eye);
		shader->setUniform("u_inverse_viewprojection", inv_vp);

		shader->setTexture("u_color_texture", gbuffers_fbo.color_textures[0], 0);
		shader->setTexture("u_normal_texture", gbuffers_fbo.color_textures[1], 1);
		shader->setTexture("u_extra_texture", gbuffers_fbo.color_textures[2], 2);
		shader->setTexture("u_depth_texture", gbuffers_fbo.depth_texture, 3);
		shader->setUniform("u_ambient_light", scene->ambient_light);

		shader->setUniform("u_first_iter", first_iter);
		//light set uniform
		if (light->light_type == POINT) {
			light->setLightUniforms(shader, false);
		}
		else
		{
			light->setLightUniforms(shader, true);
		}

		shader->setTexture("u_ao_texture", ao_buffer, 5);

		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		mesh->render(GL_TRIANGLES);
		shader->disable();
	}


	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);

	fbo.unbind();
	//Shader* final_shader = Shader::Get("finalShader"); //este solo aplica gamma
	Shader* final_shader = Shader::Get("tonemapper"); //este aplica tonemapper
	final_shader->enable();
	final_shader->setUniform("u_average_lum", average_lum);
	final_shader->setUniform("u_lumwhite2", lum_white * lum_white);
	final_shader->setUniform("u_scale", scale_tm);
	
	if (hdr) {
		fbo.color_textures[0]->toViewport(final_shader);
		final_shader->disable();
	}
	else
	{
		fbo.color_textures[0]->toViewport();
	}
	glDisable(GL_BLEND);

	if (show_gbuffers) {

		glViewport(0, h * 0.5, w*0.5, h*0.5);
		gbuffers_fbo.color_textures[0]->toViewport();
		glViewport(w * 0.5, h * 0.5, w*0.5, h*0.5);
		gbuffers_fbo.color_textures[1]->toViewport();
		glViewport(0, 0, w*0.5, h*0.5);
		gbuffers_fbo.color_textures[2]->toViewport();
		glViewport(w * 0.5, 0, w*0.5, h*0.5);

		Shader* depthShader = Shader::Get("depth");

		depthShader->enable();
		depthShader->setUniform("u_camera_nearfar", Vector2 (camera->near_plane, camera->far_plane));

		gbuffers_fbo.depth_texture->toViewport(depthShader);
		depthShader->disable();

		glViewport(0, 0, w, h);
	}
	if (show_ao_buffer)
		ao_blur_buffer->toViewport();
}


void Renderer::renderToFbo(GTR::Scene* scene, LightEntity* light) {


	Shader* shader = Shader::Get("depth");

	shader->enable();
	glDisable(GL_DEPTH_TEST);

	shader->setUniform("u_camera_nearfar", Vector2(light->camera.near_plane, light->camera.far_plane));

	light->fbo->depth_texture->toViewport(shader);

	glEnable(GL_DEPTH_TEST);
	shader->disable();
}

void Renderer::collectRenderCalls(GTR::Scene* scene, Camera* camera) {

	renderCallList.clear();

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
			if (pent->prefab)
				getRenderCallsFromPrefabs(ent->model, pent->prefab, camera);
		}
	}

	std::sort(renderCallList.begin(), renderCallList.end(), compareNodes);
}


void Renderer::renderScene(GTR::Scene* scene, Camera* camera, ePipelineMode pipmode)
{
	collectRenderCalls(scene, camera);

	if (pipmode == FORWARD)
		renderForward(scene, renderCallList, camera);
	else if (pipmode == DEFERRED)
		renderDeferred(scene, renderCallList, camera);
}

//renders all the prefab
void Renderer::getRenderCallsFromPrefabs(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
	assert(prefab && "PREFAB IS NULL");
	//assign the model to the root node
	getRenderCallsFromNode(model, &prefab->root, camera);
}

//renders a node of the prefab and its children
void Renderer::getRenderCallsFromNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
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
			addRenderCalltoList(node_model, node->mesh, node->material, camera, camera->eye.distance(world_bounding.center)); //uso el centro de la bounding box para la distancia,
						  //el resto es de la llamada normal, el alpha va en el material
			//node->mesh->renderBounding(node_model, true);
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		getRenderCallsFromNode(prefab_model, node->children[i], camera);
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(eRenderMode mode, GTR::Scene* scene, const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;
	Texture* texture = NULL;
	texture = material->color_texture.texture;
	if (texture == NULL)
		texture = Texture::getWhiteTexture(); //a 1x1 white texture

	if (rendering_shadowmap && mode == DEFAULT) {
		renderMeshInShadowMap(material, camera, model, mesh, texture);
		return;
	}

	//texture = material->emissive_texture;
	//texture = material->metallic_roughness_texture.texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;

	Texture* metallic_roughness_texture = material->metallic_roughness_texture.texture;
	if (metallic_roughness_texture == NULL)
		metallic_roughness_texture = Texture::getWhiteTexture(); //a 1x1 white texture


	Texture* emmisive_texture = material->emissive_texture.texture;
	if (emmisive_texture == NULL)
		emmisive_texture = Texture::getWhiteTexture(); //a 1x1 white texture


	Texture* normalmap = material->normal_texture.texture;
	if (!normalmap) {
		normalmap = Texture::getWhiteTexture(); //a 1x1 white texture
	}

	//select the blending
	if (material->alpha_mode == GTR::eAlphaMode::BLEND)
	{
		//if (mode == GBUFFERS) return;
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

	switch (mode)
	{
	case GTR::DEFAULT:
		shader = Shader::Get("multipasslight");
		break;
	case GTR::SHOW_TEXTURE:
		shader = Shader::Get("texture");
		break;
	case GTR::SHOW_NORMAL:
		shader = Shader::Get("normal");
		break;
	case GTR::SHOW_OCCLUSION:
		shader = Shader::Get("occlusion");
		break;
	case GTR::SHOW_UVS:
		shader = Shader::Get("uvs");
		break;
	case GTR::SINGLE:
		shader = Shader::Get("basiclight");
		break;
	case GTR::GBUFFERS:
		shader = Shader::Get("gbuffers");
		break;
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
	if(normalmap)
		shader->setUniform("u_normalmap", normalmap, 3);

	if (mode == GBUFFERS) {
		shader->setUniform("u_roughness_factor", material->roughness_factor);
		shader->setUniform("u_metallic_factor", material->metallic_factor);
		//std::cout << "metallicf: " << material->metallic_factor << " roughnessf: " << material->roughness_factor << "\n";
	}

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	if (mode == SINGLE) {
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

		
		shader->setUniform1Array("u_light_type", (int*)&light_types, 4);
		shader->setUniform3Array("u_light_pos", (float*)&light_position, 4);
		shader->setUniform3Array("u_light_target", (float*)&light_target, 4);
		shader->setUniform3Array("u_light_color", (float*)&light_color, 4);
		shader->setUniform1Array("u_light_max_dists", (float*)&light_maxdists, 4);
		shader->setUniform1Array("u_light_coscutoff", (float*)&light_coscutoff, 4);
		shader->setUniform1Array("u_light_spotexp", (float*)&light_spotexponent, 4);
		shader->setUniform("u_num_lights", 4);
		//do the draw call that renders the mesh into the screen
		mesh->render(GL_TRIANGLES);
	}

	else if (mode == DEFAULT) {
		for (size_t i = 0; i < lightsScene.size() && i < 5; i++) {
			LightEntity* light = lightsScene[i];

			light->setLightUniforms(shader, true);

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

void Renderer::renderMeshInShadowMap(Material* material, Camera* camera, Matrix44 model, Mesh* mesh, Texture* texture)
{
	Shader* shader = Shader::Get("texture");

	assert(glGetError() == GL_NO_ERROR);

	if (material->alpha_mode == GTR::BLEND) return;

	shader->enable();

	shader->setUniform("u_texture", texture, 0);
	shader->setUniform("u_color", material->color);
	shader->setUniform("u_camera_pos", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	mesh->render(GL_TRIANGLES);

}

void Renderer::renderSceneShadowmaps(GTR::Scene* scene)
{
	rendering_shadowmap = true;
	std::vector<GTR::LightEntity*> lightsScene = scene->lights;

	for (size_t i = 0; i < lightsScene.size() && i < 5; i++) {
		GTR::LightEntity* light = lightsScene[i];
		if (!light->cast_shadows) continue;
		
		if (!light->fbo) {
			light->fbo = new FBO();
			light->fbo->setDepthOnly(1024, 1024);
		}

		light->fbo->bind();
		glColorMask(false, false, false, false);

		glClear(GL_DEPTH_BUFFER_BIT);

		Camera* light_cam = &light->camera;

		//light_cam->enable();
		renderScene(scene, light_cam, FORWARD);

		light->fbo->unbind();
		glColorMask(true, true, true, true);
	}
	rendering_shadowmap = false;
}

GTR::SSAOFX::SSAOFX() {
	points = generateSpherePoints(64, 10.0, true);
	intensity = 1.0f;
}

void GTR::SSAOFX::blurTexture(Texture* input, Texture* output)
{
	FBO* fbo = Texture::getGlobalFBO(output);
	fbo->bind();

	Mesh* quad = Mesh::getQuad();
	Shader* sh = Shader::Get("blur");

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	sh->enable();
	sh->setTexture("ssaoInput", input, 9);
	quad->render(GL_TRIANGLES);
	sh->disable();

	fbo->unbind();
}

void GTR::SSAOFX::apply(Texture* depth_buffer, Texture* normal_buffer, Camera* cam, Texture* output) {

	FBO* fbo = Texture::getGlobalFBO(output);
	fbo->bind();

	Matrix44 innvp = cam->viewprojection_matrix;
	innvp.inverse();

	Mesh* quad = Mesh::getQuad();

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	Shader* sh = Shader::Get("ssao");
	sh->enable();
	sh->setUniform("u_inverse_viewprojection", innvp);
	sh->setUniform("u_iRes", Vector2(1.0 / (float)depth_buffer->width, 1.0 / (float)depth_buffer->height));
	sh->setUniform3Array("u_points", points[0].v, points.size());

	sh->setUniform("u_viewprojection", cam->viewprojection_matrix);
	sh->setTexture("u_normal_texture", normal_buffer, 1);
	sh->setTexture("u_depth_texture", depth_buffer, 3);
	quad->render(GL_TRIANGLES);

	sh->disable();

	fbo->unbind();
}

std::vector<Vector3> GTR::generateSpherePoints(int num,
	float radius, bool hemi)
{
	std::vector<Vector3> points;
	points.resize(num);
	for (int i = 0; i < num; i += 3)
	{
		Vector3& p = points[i];
		float u = random();
		float v = random();
		float theta = u * 2.0 * PI;
		float phi = acos(2.0 * v - 1.0);
		float r = cbrt(random() * 0.9 + 0.1) * radius;
		float sinTheta = sin(theta);
		float cosTheta = cos(theta);
		float sinPhi = sin(phi);
		float cosPhi = cos(phi);
		p.x = r * sinPhi * cosTheta;
		p.y = r * sinPhi * sinTheta;
		p.z = r * cosPhi;
		if (hemi && p.z < 0)
			p.z *= -1.0;
	}
	return points;
}


void GTR::Renderer::renderInMenu() {

	if (pipeline_mode == GTR::ePipelineMode::DEFERRED) {
		ImGui::Checkbox("Show gbuffers", &show_gbuffers);
		ImGui::Checkbox("Show SSAO", &show_ao_buffer);
		ImGui::Checkbox("Apply tonemap", &hdr);
		if (hdr) {
			ImGui::SliderFloat("Average luminance", &average_lum, 0.0, 2.0);
			ImGui::SliderFloat("White luminance", &lum_white, 0.0, 2.0);
			ImGui::SliderFloat("Scale tonemap", &scale_tm, 0.001, 2.0);
		}
	}
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
