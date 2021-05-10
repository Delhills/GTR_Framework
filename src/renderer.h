#pragma once
#include "prefab.h"

//forward declarations
class Camera;

namespace GTR {

	enum eRenderMode {
		DEFAULT,
		SHOW_TEXTURE,
		SHOW_NORMAL,
		SHOW_OCCLUSION,
		SHOW_UVS,
		MULTI
	};

	class Prefab;
	class Material;

	struct renderCall { //recuerda añadir el tipo (prefab, ...)
		Matrix44 model;
		Mesh* mesh;
		Material* material;
		Camera* camera;
		float distance_to_cam;
	};

	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{

	public:

		//add here your functions
		//...
		eRenderMode render_mode;
		eLightType light_types[5];
		Vector3 light_position[5];
		Vector3 light_target[5];
		Vector3 light_color[5];
		float light_maxdists[5];
		float light_coscutoff[5];
		float light_spotexponent[5];
		Vector3 light_vector[5];

		Renderer();

		std::vector<renderCall> renderCallList;
		//renders several elements of the scene
		void addRenderCalltoList(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera, float dist);

		void renderScene(GTR::Scene* scene, Camera* camera);
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
	};

	Texture* CubemapFromHDRE(const char* filename);

};