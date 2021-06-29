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
#include "sphericalharmonics.h"

using namespace GTR;

sProbe probe;

Renderer::Renderer() {
	w = Application::instance->window_width;
	h = Application::instance->window_height;

	average_lum = 1.0;
	lum_white = 1.0;
	scale_tm = 1.0;

	render_mode = eRenderMode::DEFAULT;
	pipeline_mode = ePipelineMode::DEFERRED;
	blend_mode = DITHERING;

	fbo.create(w, h, 1, GL_RGBA, GL_FLOAT, true);

	//memset(&probe, 0, sizeof(probe));
	//probe.pos.set(76, 38, 96);
	//probe.sh.coeffs[0].set(1, 0, 0);
	//probe.sh.coeffs[1].set(0, 1, 0);

	if (apply_irr)
	{
		irr_fbo = NULL;

		irr_start_pos = Vector3(-230, 36, -342);
		irr_end_pos = Vector3(530, 433, 460);
		irr_dim = Vector3(8, 6, 12);
		irr_delta = (irr_end_pos - irr_start_pos);
		irr_normal_dist = 1.0;

		defineAndPosGridProbe(GTR::Scene::instance);
	}
}


void Renderer::render(GTR::Scene* scene, Camera* camera) {

	renderSceneShadowmaps(scene);

	if (show_fbo) {
		if (showCameraDirectional) 
			renderToFbo(scene, scene->lights[3]);
		else 
			renderToFbo(scene, scene->lights[0]);
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
		renderMeshWithMaterial(render_mode, scene, rc.model, rc.mesh, rc.material, camera);
	}
}

void Renderer::renderDeferred(GTR::Scene* scene, std::vector <renderCall>& rendercalls, Camera* camera) {

	int w = Application::instance->window_width;
	int h = Application::instance->window_height;


	if (gbuffers_fbo.fbo_id == 0) {
		gbuffers_fbo.create(w, h, 3, GL_RGBA,GL_UNSIGNED_BYTE);
		decals_fbo.create(w, h, 3, GL_RGBA, GL_UNSIGNED_BYTE);
	}

	gbuffers_fbo.bind();

		renderGBuffers(scene, rendercalls, camera);

	gbuffers_fbo.unbind();

	gbuffers_fbo.color_textures[0]->copyTo(decals_fbo.color_textures[0]);
	gbuffers_fbo.color_textures[1]->copyTo(decals_fbo.color_textures[1]);
	gbuffers_fbo.color_textures[2]->copyTo(decals_fbo.color_textures[2]);

	decals_fbo.bind();

		gbuffers_fbo.depth_texture->copyTo(NULL);
		renderDecalls(scene, camera);

	decals_fbo.unbind();

	decals_fbo.color_textures[0]->copyTo(gbuffers_fbo.color_textures[0]);
	decals_fbo.color_textures[1]->copyTo(gbuffers_fbo.color_textures[1]);
	decals_fbo.color_textures[2]->copyTo(gbuffers_fbo.color_textures[2]);

	if (!ao_buffer)
		ao_buffer = new Texture(w, h, GL_RED, GL_UNSIGNED_BYTE);

	if (!ao_blur_buffer)
		ao_blur_buffer = new Texture(w, h, GL_RED, GL_UNSIGNED_BYTE);

	if (apply_ssao) {
		ssao.apply(gbuffers_fbo.depth_texture, gbuffers_fbo.color_textures[1], camera, ao_buffer);
		ssao.blurTexture(ao_buffer, ao_blur_buffer);
	}

	fbo.bind();	//textura final pre hdr
	renderFinalFBO(&gbuffers_fbo, camera, scene, hdr, ao_buffer, rendercalls);
	fbo.unbind();

	if (!fx_blur_buffer)
		fx_blur_buffer = new Texture(w, h, GL_RGBA, GL_FLOAT);

	if (!fx_threshold_buffer)
		fx_threshold_buffer = new Texture(w, h, GL_RGBA, GL_FLOAT);

	if (!fx_bloom_buffer)
		fx_bloom_buffer = new Texture(w, h, GL_RGBA, GL_FLOAT);

	if (!fx_aa_buffer)
		fx_aa_buffer = new Texture(w, h, GL_RGBA, GL_FLOAT);

	if (!fx_dof_buffer)
		fx_dof_buffer = new Texture(w, h, GL_RGBA, GL_FLOAT);

	if (!fx_dof_blurred_buffer)
		fx_dof_blurred_buffer = new Texture(w, h, GL_RGBA, GL_FLOAT);

	// Aplicam AA
	fx.aa(fbo.color_textures[0], fx_aa_buffer);

	// Bloom
	fx.treshold(fx_aa_buffer, fx_threshold_buffer);
	fx.blur(fx_threshold_buffer, fx_blur_buffer);
	fx.bloom(fx_aa_buffer, fx_blur_buffer, fx_bloom_buffer);

	// Aplicam DoF
	fx.blur(fx_bloom_buffer, fx_dof_blurred_buffer);
	fx.blur(fx_dof_blurred_buffer, fx_aa_buffer);
	fx.dof(fx_bloom_buffer, fx_aa_buffer, fbo.depth_texture, camera, fx_dof_buffer);

	Shader* final_shader = Shader::Get("tonemapper"); //este aplica tonemapper

	final_shader->enable();
	final_shader->setUniform("u_average_lum", average_lum);
	final_shader->setUniform("u_lumwhite2", lum_white * lum_white);
	final_shader->setUniform("u_scale", scale_tm);

	if (hdr)
		fx_dof_buffer->toViewport(final_shader);
		//fx_bloom_buffer->toViewport(final_shader);
	else
		decals_fbo.color_textures[0]->toViewport();

	glDisable(GL_BLEND);

	final_shader->disable();

	if (show_gbuffers)
		view_gbuffers(camera);
	else if (show_ao_buffer)
		ao_blur_buffer->toViewport();
	else if (show_depthfbo) {
		Shader* depthShader = Shader::Get("depth");

		depthShader->enable();
		depthShader->setUniform("u_camera_nearfar", Vector2(camera->near_plane, camera->far_plane));

		fbo.depth_texture->toViewport(depthShader);
		depthShader->disable();

		glViewport(0, 0, w, h);
	}

}

void Renderer::renderFinalFBO(FBO* gbuffers_fbo, Camera* camera, GTR::Scene* scene, bool hdr, Texture* ao_buffer, std::vector <renderCall>& rendercalls) {

	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);

	Mesh* mesh;
	Shader* shader;

	Mesh* sphere = Mesh::Get("data/meshes/sphere.obj", true);
	Mesh* quad = Mesh::getQuad();

	gbuffers_fbo->depth_texture->copyTo(NULL);

	for (size_t i = 0; i < scene->lights.size(); i++) {

		LightEntity* light = scene->lights[i];

		shader = Shader::Get("deferred");
		shader->enable();
		mesh = quad;
		bool first_iter = (i == 0);

		setUniformsLight(light, camera, scene, ao_buffer, shader, hdr, gbuffers_fbo, first_iter);

		shader->setUniform("u_apply_irr", apply_irr);
		shader->setUniform("u_tri_irr", apply_tri_irr);
		shader->setUniform("u_num_probes", (float)probes.size());
		shader->setUniform("u_irr_start", irr_start_pos);
		shader->setUniform("u_irr_end", irr_end_pos);
		shader->setUniform("u_irr_dims", irr_dim);
		shader->setUniform("u_irr_delta", irr_delta);
		shader->setUniform("u_irr_normal_dist", irr_normal_dist);
		shader->setUniform("u_probes_texture", probes_texture, 6);


		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);

		mesh->render(GL_TRIANGLES);

		shader->disable();
	}

	if (blend_mode == FORWARD_BLEND) {
		glEnable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		for (size_t i = 0; i < rendercalls.size(); i++)
		{
			renderCall& rc = rendercalls[i];
			if (rc.material->alpha_mode != eAlphaMode::BLEND) continue;
			renderMeshWithMaterial(eRenderMode::SINGLE, scene, rc.model, rc.mesh, rc.material, camera);
		}

	}

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);

	computeVolumetric(camera, gbuffers_fbo->depth_texture, scene);

	int numProbes = probes.size();
	//now compute the coeffs for every probe
	for (int iP = 0; iP < numProbes; ++iP) {
		int probe_index = iP;
		renderProbe(probes[iP].pos, 3.0, probes[iP].sh.coeffs[0].v);
	}

	if (irr_fbo && show_irr_fbo) {
		irr_fbo->color_textures[0]->toViewport();
	}
	if (show_probes_text && probes_texture) {
		probes_texture->toViewport();
	}

}

void Renderer::view_gbuffers(Camera* camera) {

	//FBO* fbo = &gbuffers_fbo;
	FBO* fbo = &decals_fbo;

	glViewport(0, h * 0.5, w * 0.5, h * 0.5);
	fbo->color_textures[0]->toViewport();
	glViewport(w * 0.5, h * 0.5, w * 0.5, h * 0.5);
	fbo->color_textures[1]->toViewport();
	glViewport(0, 0, w * 0.5, h * 0.5);
	fbo->color_textures[2]->toViewport();
	glViewport(w * 0.5, 0, w * 0.5, h * 0.5);

	Shader* depthShader = Shader::Get("depth");

	depthShader->enable();
	depthShader->setUniform("u_camera_nearfar", Vector2(camera->near_plane, camera->far_plane));

	fbo->depth_texture->toViewport(depthShader);
	depthShader->disable();

	glViewport(0, 0, w, h);
}

void Renderer::computeVolumetric(Camera* camera, Texture* depth_texture, Scene* scene) {
	// Volumetric rendering

	Mesh* quad_volum = Mesh::getQuad();
	Shader* sh = Shader::Get("volume_direct");
	sh->enable();

	Matrix44 inv_vp = camera->viewprojection_matrix;
	inv_vp.inverse();

	sh->setUniform("u_inverse_viewprojection", inv_vp);
	sh->setTexture("u_depth_texture", depth_texture, 3);
	sh->setUniform("u_camera_pos", camera->eye);

	LightEntity* light = scene->lights[3];
	light->setLightUniforms(sh, true);

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);

	quad_volum->render(GL_TRIANGLES);
	glDisable(GL_BLEND);

	sh->disable();
}

void Renderer::setUniformsLight(LightEntity* light, Camera* camera, GTR::Scene* scene, Texture* ao_buffer, Shader* shader, bool hdr, FBO* gbuffers_fbo, bool first_iter) {

	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);

	Matrix44 inv_vp = camera->viewprojection_matrix;
	inv_vp.inverse();

	shader->setUniform("hdr", hdr);

	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_inverse_viewprojection", inv_vp);

	shader->setTexture("u_color_texture", gbuffers_fbo->color_textures[0], 0);

	shader->setTexture("u_normal_texture", gbuffers_fbo->color_textures[1], 1);
	shader->setTexture("u_extra_texture", gbuffers_fbo->color_textures[2], 2);
	shader->setTexture("u_depth_texture", gbuffers_fbo->depth_texture, 3);

	if (first_iter)
		shader->setUniform("u_ambient_light", scene->ambient_light);
	else 
		shader->setUniform("u_ambient_light", Vector3());

	shader->setUniform("u_first_iter", first_iter);

	if (light->light_type == eLightType(0))
		light->setLightUniforms(shader, false);
	else
		light->setLightUniforms(shader, true);

	if (apply_ssao)
		shader->setTexture("u_ao_texture", ao_buffer, 5);

	shader->setUniform("u_apply_ssao", apply_ssao);
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
	if (camera)
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
		if (!camera || camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize))
		{

			renderCall aux;

			aux.model = node_model;
			aux.mesh = node->mesh;
			aux.material = node->material;
			if(camera)
				aux.distance_to_cam = camera->eye.distance(world_bounding.center);
			//uso el centro de la bounding box para la distancia

			this->renderCallList.push_back(aux);

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

	if (blend_mode == DITHERING) shader->setUniform("u_dithering", true);
	if (blend_mode == FORWARD_BLEND) shader->setUniform("u_dithering", false);

	//upload uniforms
	if (camera) {
		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		shader->setUniform("u_camera_position", camera->eye);
	}

	shader->setUniform("u_model", model );
	float t = getTime();
	shader->setUniform("u_time", t );

	shader->setUniform("u_color", material->color);
	shader->setUniform("u_emissive_factor", material->emissive_factor);

	if (blend_mode != FORWARD_BLEND)
		shader->setUniform("u_ambient_light", scene->ambient_light);
	else
		shader->setUniform("u_ambient_light", Vector3(0.0, 0.0, 0.0));

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
			light_color[i] = lightsScene[i]->color;
			light_intensity[i] = lightsScene[i]->intensity;
			light_position[i] = lightsScene[i]->model.getTranslation();
			light_maxdists[i] = lightsScene[i]->max_distance;
			light_coscutoff[i] = cos((lightsScene[i]->cone_angle / 180.0) * PI);
			light_spotexponent[i] = lightsScene[i]->spot_exponent;
			light_target[i] = lightsScene[i]->model.frontVector();
			light_vector[i] = lightsScene[i]->target;
		}

		int numlights = lightsScene.size();
		shader->setUniform1Array("u_light_type", (int*)&light_types, numlights);
		shader->setUniform3Array("u_light_pos", (float*)&light_position, numlights);
		shader->setUniform3Array("u_light_target", (float*)&light_target, numlights);
		shader->setUniform3Array("u_light_color", (float*)&light_color, numlights);
		shader->setUniform1Array("u_light_max_dists", (float*)&light_maxdists, numlights);
		shader->setUniform1Array("u_light_coscutoff", (float*)&light_coscutoff, numlights);
		shader->setUniform1Array("u_light_spotexp", (float*)&light_spotexponent, numlights);
		shader->setUniform1Array("u_light_spotexp", (float*)&light_intensity, numlights);
		shader->setUniform("u_num_lights", numlights);
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
		renderScene(scene, light_cam, GTR::ePipelineMode::FORWARD);

		light->fbo->unbind();
		glColorMask(true, true, true, true);
	}
	rendering_shadowmap = false;
}

void Renderer::renderDecalls(GTR::Scene* scene, Camera* camera) { 

	static Mesh* mesh = NULL;

	if (mesh == NULL) {
		mesh = new Mesh();
		mesh->createCube();
	}

	Shader* shader = Shader::Get("decalls");

	shader->enable();

	Matrix44 inv_vp = camera->viewprojection_matrix;
	inv_vp.inverse();
	shader->setUniform("u_inverse_viewprojection", inv_vp);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);

	shader->setUniform("u_camera_pos", camera->eye);
	shader->setUniform("u_iRes", Vector2(1.0 / (float)gbuffers_fbo.depth_texture->width, 1.0 / (float)gbuffers_fbo.depth_texture->height));

	shader->setTexture("u_color_texture", gbuffers_fbo.color_textures[0], 0);
	shader->setTexture("u_normal_texture", gbuffers_fbo.color_textures[1], 1);
	shader->setTexture("u_extra_texture", gbuffers_fbo.color_textures[2], 2);
	shader->setTexture("u_depth_texture", gbuffers_fbo.depth_texture, 3);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	for (int i = 0; i < scene->entities.size(); ++i) {
		BaseEntity* ent = scene->entities[i];
		if (ent->entity_type != eEntityType::DECAL)
			continue;
		
		DecalEntity* dent = (DecalEntity*) ent;

		shader->setUniform("u_model", ent->model);

		Matrix44 inv_model = ent->model;
		inv_model.inverse();
		shader->setUniform("u_iModel", inv_model);
		shader->setTexture("u_decal_texture", dent->albedo, 4);

		mesh->render(GL_TRIANGLES);
	}

	shader->disable();
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
		ImGui::Checkbox("Show depth fbo", &show_depthfbo);
		ImGui::Checkbox("Apply tonemap", &hdr);
		ImGui::Checkbox("Apply irr", &apply_irr);
		if (apply_irr)
		{
			ImGui::Checkbox("Apply trilinear interpolation irr", &apply_tri_irr);
		}
		ImGui::Checkbox("Show probes_text", &show_probes_text);
		if (hdr) {
			ImGui::SliderFloat("Average luminance", &average_lum, 0.0, 2.0);
			ImGui::SliderFloat("White luminance", &lum_white, 0.0, 2.0);
			ImGui::SliderFloat("Scale tonemap", &scale_tm, 0.001, 2.0);
		}
		ImGui::Combo("Blend", (int*)&blend_mode, "DITHERING\0FORWARD", 2);
		ImGui::SliderFloat("Treshold intensity", &fx.treshold_intensity, 0, 10);
		ImGui::SliderFloat("Bloom intensity", &fx.bloom_intensity, 0, 10);
		ImGui::SliderFloat("focal intesity", &fx.focal_intensity, 0, 1000);
		ImGui::SliderFloat("min distance", &fx.min_distance, 0, 1000);
		ImGui::SliderFloat("max distance", &fx.max_distance, 0, 1000);
	}
}


void GTR::Renderer::renderGBuffers(Scene* scene, std::vector <renderCall>& rendercalls, Camera* camera)
{
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	checkGLErrors();

	for (size_t i = 0; i < rendercalls.size(); i++)
	{
		renderCall& rc = rendercalls[i];
		if (blend_mode == FORWARD_BLEND && rc.material->alpha_mode == eAlphaMode::BLEND)
			continue;
		renderMeshWithMaterial(eRenderMode::GBUFFERS, scene, rc.model, rc.mesh, rc.material, camera);
	}
}


void GTR::Renderer::defineAndPosGridProbe(GTR::Scene* scene)
{
	//when computing the probes position�

	//define the corners of the axis aligned grid
	//this can be done using the boundings of our scene

	//define how many probes you want per dimension -230, 36, -342 //500 433 460

	//compute the vector from one corner to the other

	//and scale it down according to the subdivisions
	//we substract one to be sure the last probe is at end pos
	irr_delta.x /= (irr_dim.x - 1);
	irr_delta.y /= (irr_dim.y - 1);
	irr_delta.z /= (irr_dim.z - 1);

	//now delta give us the distance between probes in every axis

	//lets compute the centers
	//pay attention at the order at which we add them
	for (int z = 0; z < irr_dim.z; ++z)
		for (int y = 0; y < irr_dim.y; ++y)
			for (int x = 0; x < irr_dim.x; ++x)
			{
				sProbe p;
				p.local.set(x, y, z);

				//index in the linear array
				p.index = x + y * irr_dim.x + z * irr_dim.x * irr_dim.y;

				//and its position
				p.pos = irr_start_pos + irr_delta * Vector3(x, y, z);
				probes.push_back(p);
			}

	if (!probes_texture)
	{
		int numProb = probes.size();
		probes_texture = new Texture(
			9, //9 coefficients per probe
			numProb, //as many rows as probes
			GL_RGB, //3 channels per coefficient
			GL_FLOAT); //they require a high range
		updateIrradianceCache(scene);
	}
}


void Renderer::renderProbe(Vector3 pos, float size, float* coeffs)
{
	Camera* camera = Camera::current;
	Shader* shader = Shader::Get("probe");
	Mesh* mesh = Mesh::Get("data/meshes/sphere.obj");

	glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	Matrix44 model;
	model.setTranslation(pos.x, pos.y, pos.z);
	model.scale(size, size, size);

	shader->enable();
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform3Array("u_coeffs", coeffs, 9);

	mesh->render(GL_TRIANGLES);

}


void Renderer::extractProbe(GTR::Scene* scene, sProbe& p) {
	FloatImage images[6]; //here we will store the six views
	Camera cam;

	//set the fov to 90 and the aspect to 1
	cam.setPerspective(90, 1, 0.1, 1000);

	if (!irr_fbo) {
		irr_fbo = new FBO();
		irr_fbo->create(64, 64, 1, GL_RGB, GL_FLOAT);
	}

	collectRenderCalls(scene, NULL);
	//std::cout << renderCallList.size() << "\n";

	for (int i = 0; i < 6; i++) //for every cubemap face
	{
		//compute camera orientation using defined vectors
		Vector3 eye = p.pos;
		Vector3 front = cubemapFaceNormals[i][2];
		Vector3 center = p.pos + front;
		Vector3 up = cubemapFaceNormals[i][1];
		cam.lookAt(eye, center, up);
		cam.enable();

		//render the scene from this point of view
		irr_fbo->bind();
		renderForward(scene, renderCallList, &cam);
		irr_fbo->unbind();

		//read the pixels back and store in a FloatImage
		images[i].fromTexture(irr_fbo->color_textures[0]);
	}

	//compute the coefficients given the six images
	p.sh = computeSH(images, 1.0);
}

void GTR::Renderer::updateIrradianceCache(GTR::Scene* scene) {

	//we must create the color information for the texture. because every SH are 27 floats in the RGB,RGB,... order, we can create an array of SphericalHarmonics and use it as pixels of the texture
	SphericalHarmonics* sh_data = NULL;
	sh_data = new SphericalHarmonics[ irr_dim.x * irr_dim.y * irr_dim.z ];

	int numProbes = probes.size();
	//now compute the coeffs for every probe
	for (int iP = 0; iP < numProbes; ++iP)
	{
		extractProbe(scene, probes[iP]);
		sh_data[iP] = probes[iP].sh;
	}


	//here we fill the data of the array with our probes in x,y,z order...
	//now upload the data to the GPU
	probes_texture->upload( GL_RGB, GL_FLOAT, false, (uint8*)sh_data);

	////disable any texture filtering when reading
	probes_texture->bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	////always free memory after allocating it!!!
	delete[] sh_data;
	//extractProbe(scene, probe);
}

Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = HDRE::Get(filename);
	if (!hdre)
		return NULL;

	Texture* texture = new Texture();
	if (hdre->getFacesf(0))
	{
		texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesf(0),
			hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT);
		for (int i = 1; i < hdre->levels; ++i)
			texture->uploadCubemap(texture->format, texture->type, false,
				(Uint8**)hdre->getFacesf(i), GL_RGBA32F, i);
	}
	else
		if (hdre->getFacesh(0))
		{
			texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesh(0),
				hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_HALF_FLOAT);
			for (int i = 1; i < hdre->levels; ++i)
				texture->uploadCubemap(texture->format, texture->type, false,
					(Uint8**)hdre->getFacesh(i), GL_RGBA16F, i);
		}
	return texture;
}

GTR::FX::FX() {
	treshold_intensity = 1.0f;
	bloom_intensity = 1.0f;
	focal_intensity = 1.0f;

	float min_distance = 8;
	float max_distance = 12;

	w = Application::instance->window_width;;
	h = Application::instance->window_height;;
}

void GTR::FX::blur(Texture* input, Texture* output) {
	setFX(BLUR, input, output);
}

void GTR::FX::treshold(Texture* input, Texture* output) {
	setFX(TRESHOLD, input, output);
}

void GTR::FX::bloom(Texture* input_base, Texture* input_blurred, Texture* output) {
	setFX(BLOOM, input_base, output, input_blurred);
}

void GTR::FX::aa(Texture* input, Texture* output) {
	setFX(AA, input, output);
}

void GTR::FX::dof(Texture* input, Texture* input_blurred, Texture* depth_buffer, Camera* camera, Texture* output) {
	setFX(DOF, input, output, input_blurred, depth_buffer, camera);
}

void GTR::FX::setFX(eFxMode fx, Texture* input, Texture* output, Texture* second_input, Texture* depth_buffer, Camera* camera)
{
	FBO* fbo = Texture::getGlobalFBO(output);
	fbo->bind();

		Mesh* quad = Mesh::getQuad();
		Shader* sh;
		switch (fx) {
			case AA: sh = Shader::Get("AAFX"); break;
			case TRESHOLD: sh = Shader::Get("tresholdFX"); break;
			case BLUR: sh = Shader::Get("blurFX"); break;
			case BLOOM: sh = Shader::Get("bloomFX"); break;
			case DOF: sh = Shader::Get("DOFFX"); break;
		}

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);

		sh->enable();

			sh->setTexture("u_input", input, 9);

			switch (fx) {
				case AA: 
					sh->setUniform("u_iViewportSize", Vector2(1.0 / (float)w, 1.0 / (float)h));
					sh->setUniform("u_viewportSize", Vector2((float)w, (float)h));
					break;
				case TRESHOLD:
					sh->setUniform("u_treshold_intensity", treshold_intensity);
					break;
				case BLOOM: 
					sh->setTexture("u_input_blurred", second_input, 10);
					sh->setUniform("u_bloom_intensity", bloom_intensity);
					break;
				case DOF:
					sh->setTexture("u_input_blurred", second_input, 10);
					sh->setTexture("u_depth_buffer", depth_buffer, 11);

					Matrix44 inv_vp = camera->viewprojection_matrix;
					inv_vp.inverse();
					sh->setUniform("u_inverse_viewprojection", inv_vp);
					sh->setUniform("u_camera_pos", camera->eye);

					sh->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));

					sh->setUniform("u_min_distance", min_distance);
					sh->setUniform("u_max_distance", max_distance);
			}

			quad->render(GL_TRIANGLES);

		sh->disable();

	fbo->unbind();

}
