#include "scene.h"
#include "utils.h"
#include "application.h"
#include "prefab.h"
#include "shader.h"
#include "extra/cJSON.h"

GTR::Scene* GTR::Scene::instance = NULL;

GTR::Scene::Scene()
{
	instance = this;
}

void GTR::Scene::clear()
{
	for (int i = 0; i < entities.size(); ++i)
	{
		BaseEntity* ent = entities[i];
		delete ent;
	}
	entities.resize(0);
}


void GTR::Scene::addEntity(BaseEntity* entity)
{
	entities.push_back(entity); entity->scene = this;
}

bool GTR::Scene::load(const char* filename)
{
	std::string content;

	this->filename = filename;
	std::cout << " + Reading scene JSON: " << filename << "..." << std::endl;

	if (!readFile(filename, content))
	{
		std::cout << "- ERROR: Scene file not found: " << filename << std::endl;
		return false;
	}

	//parse json string
	cJSON* json = cJSON_Parse(content.c_str());
	if (!json)
	{
		std::cout << "ERROR: Scene JSON has errors: " << filename << std::endl;
		return false;
	}

	//read global properties
	background_color = readJSONVector3(json, "background_color", background_color);
	ambient_light = readJSONVector3(json, "ambient_light", ambient_light );
	main_camera.eye = readJSONVector3(json, "camera_position", main_camera.eye);
	main_camera.center = readJSONVector3(json, "camera_target", main_camera.center);
	main_camera.fov = readJSONNumber(json, "camera_fov", main_camera.fov);

	//entities
	cJSON* entities_json = cJSON_GetObjectItemCaseSensitive(json, "entities");
	cJSON* entity_json;
	cJSON_ArrayForEach(entity_json, entities_json)
	{
		std::string type_str = cJSON_GetObjectItem(entity_json, "type")->valuestring;
		BaseEntity* ent = createEntity(type_str);
		if (!ent)
		{
			std::cout << " - ENTITY TYPE UNKNOWN: " << type_str << std::endl;
			//continue;
			ent = new BaseEntity();
		}

		addEntity(ent);

		if (cJSON_GetObjectItem(entity_json, "name"))
		{
			ent->name = cJSON_GetObjectItem(entity_json, "name")->valuestring;
			stdlog(std::string(" + entity: ") + ent->name);
		}

		//read transform
		if (cJSON_GetObjectItem(entity_json, "position"))
		{
			ent->model.setIdentity();
			Vector3 position = readJSONVector3(entity_json, "position", Vector3());
			ent->model.translate(position.x, position.y, position.z);
		}

		if (cJSON_GetObjectItem(entity_json, "angle"))
		{
			float angle = cJSON_GetObjectItem(entity_json, "angle")->valuedouble;
			ent->model.rotate(angle * DEG2RAD, Vector3(0, 1, 0));
		}

		if (cJSON_GetObjectItem(entity_json, "rotation"))
		{
			Vector4 rotation = readJSONVector4(entity_json, "rotation");
			Quaternion q(rotation.x, rotation.y, rotation.z, rotation.w);
			Matrix44 R;
			q.toMatrix(R);
			ent->model = R * ent->model;
		}

		if (cJSON_GetObjectItem(entity_json, "scale"))
		{
			Vector3 scale = readJSONVector3(entity_json, "scale", Vector3(1, 1, 1));
			ent->model.scale(scale.x, scale.y, scale.z);
		}

		ent->configure(instance, entity_json);
	}

	//free memory
	cJSON_Delete(json);

	return true;
}

GTR::BaseEntity* GTR::Scene::createEntity(std::string type)
{
	if (type == "PREFAB")
		return new GTR::PrefabEntity();
	if (type == "LIGHT")
		return new GTR::LightEntity();
    return NULL;
}

void GTR::BaseEntity::renderInMenu()
{
#ifndef SKIP_IMGUI
	ImGui::Text("Name: %s", name.c_str()); // Edit 3 floats representing a color
	ImGui::Checkbox("Visible", &visible); // Edit 3 floats representing a color
	//Model edit
	ImGuiMatrix44(model, "Model");
#endif
}




GTR::PrefabEntity::PrefabEntity()
{
	entity_type = PREFAB;
	prefab = NULL;
}

void GTR::PrefabEntity::configure(GTR::Scene* scene, cJSON* json)
{
	if (cJSON_GetObjectItem(json, "filename"))
	{
		filename = cJSON_GetObjectItem(json, "filename")->valuestring;
		prefab = GTR::Prefab::Get( (std::string("data/") + filename).c_str());
	}
}

void GTR::PrefabEntity::renderInMenu()
{
	BaseEntity::renderInMenu();

#ifndef SKIP_IMGUI
	ImGui::Text("filename: %s", filename.c_str()); // Edit 3 floats representing a color
	if (prefab && ImGui::TreeNode(prefab, "Prefab Info"))
	{
		prefab->root.renderInMenu();
		ImGui::TreePop();
	}
#endif
}

GTR::LightEntity::LightEntity()
{
	entity_type = LIGHT;
	fbo = NULL;
}

void GTR::LightEntity::configure(GTR::Scene* scene, cJSON* json)
{
	std::string lighttype = readJSONString(json, "lighttype", "point");

	if (lighttype == "point") light_type = GTR::eLightType::POINT;
	else if (lighttype == "spot") light_type = GTR::eLightType::SPOT;
	else if (lighttype == "directional") light_type = GTR::eLightType::DIRECTIONAL;

	color = readJSONVector3(json, "color", Vector3(1.0, 0, 1.0));
	intensity = readJSONNumber(json, "intensity", 1.0);
	max_distance = readJSONNumber(json, "max_dist", 1000.0);


	target = readJSONVector3(json, "target", (model.getTranslation() + model.frontVector())) - model.getTranslation();
	model.setFrontAndOrthonormalize(target);

	cast_shadows = readJSONBool(json, "cast_shadows", false);
	shadow_bias = readJSONNumber(json, "shadow_bias", 0.001);
	area_size = readJSONNumber(json, "area_size", 1500);


	cone_angle = readJSONNumber(json, "cone_angle", 30.0);
	spot_exponent = readJSONNumber(json, "cone_exp", 10.0);

	if (light_type == SPOT) {

		if (cast_shadows) {
			float width = Application::instance->window_width;
			float height = Application::instance->window_height;
			Vector3 lightpos = model.getTranslation();
			camera.lookAt(lightpos, lightpos + model.frontVector(), Vector3(0, 1.001, 0));
			camera.setPerspective(2 * cone_angle, width / (float)height, 1.0f, max_distance);
		}
	}
	if (light_type == DIRECTIONAL) {
		Vector3 lightpos = model.getTranslation();
		if (cast_shadows) {
			camera.lookAt(lightpos, lightpos + model.frontVector(), Vector3(0, 1.001, 0));
			camera.setOrthographic(-area_size, area_size, -area_size, area_size, -500, max_distance);
		}
	}

	scene->lights.push_back(this);
}
void GTR::LightEntity::renderInMenu()
{
	BaseEntity::renderInMenu();

#ifndef SKIP_IMGUI
	ImGui::ColorEdit3("Color", color.v);
	ImGui::SliderFloat("Intensity", &intensity, 0, 10);
	ImGui::SliderFloat("Max distance", &max_distance, 0, 3700);
	ImGui::Checkbox("cast shadow", &cast_shadows);

	if (light_type == GTR::eLightType::SPOT)
	{
		ImGui::SliderFloat("Cone angle", &cone_angle, 0, 89);
		ImGui::SliderFloat("Spot exponent", &spot_exponent, 0, 100);
		Vector3 lightpos = model.getTranslation();
		//camera.lookAt(lightpos, lightpos + this->model.frontVector(), Vector3(0, 1.001, 0));
		camera.lookAt(lightpos, lightpos + model.frontVector(), model.topVector());
	}
	if (light_type == GTR::eLightType::DIRECTIONAL)
	{
		ImGui::SliderFloat("Area size", &area_size, 0, 2000);
		ImGui::SliderFloat("Shadow bias", &shadow_bias, -1.00000, 1.00000);
		Vector3 lightpos = model.getTranslation();
		//camera.lookAt(lightpos, lightpos + this->model.frontVector(), Vector3(0, 1.001, 0));
		camera.lookAt(lightpos, lightpos + model.frontVector(), model.topVector());
		camera.setOrthographic(-area_size, area_size, -area_size, area_size, 0.1f, 5000.f);
	}
#endif
}
