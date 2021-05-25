#pragma once
#include "prefab.h"
#include "fbo.h"

//forward declarations
class Camera;

namespace GTR {

	enum eRenderMode {
		DEFAULT,
		SHOW_TEXTURE,
		SHOW_NORMAL,
		SHOW_OCCLUSION,
		SHOW_UVS,
		SINGLE,
		GBUFFERS
	};
	
	enum ePipelineMode {
		FORWARD,
		DEFERRED
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
		FBO fbo;
		FBO gbuffers_fbo;
		Texture* color_buffer;

		bool rendering_shadowmap;
		eRenderMode render_mode;
		ePipelineMode pipeline_mode;
		eLightType light_types[5];
		Vector3 light_position[5];
		Vector3 light_target[5];
		Vector3 light_color[5];
		float light_maxdists[5];
		float light_coscutoff[5];
		float light_spotexponent[5];
		Vector3 light_vector[5];
		bool show_fbo = false;
		bool show_gbuffers = false;
		bool showCameraDirectional = false;

		Renderer();

		std::vector<renderCall> renderCallList;
		//renders several elements of the scene
		void addRenderCalltoList(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera, float dist);

		void render(GTR::Scene* scene, Camera* camera);


		void renderScene(GTR::Scene* scene, Camera* camera);

		void collectRenderCalls(GTR::Scene* scene, Camera* camera);

		void renderScene(GTR::Scene* scene, Camera* camera, ePipelineMode pipmode);

		void renderForward(GTR::Scene* scene, std::vector <renderCall>& rendercalls, Camera* camera);

		void renderDeferred(GTR::Scene* scene, std::vector <renderCall>& rendercalls, Camera* camera);
	
		//to render a whole prefab (with all its nodes)
		void getRenderCallsFromPrefabs(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void getRenderCallsFromNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(eRenderMode mode, GTR::Scene* scene, const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		void renderSceneShadowmaps(GTR::Scene* scene);

		void renderMeshInShadowMap(Material* material, Camera* camera, Matrix44 model, Mesh* mesh, Texture* texture);

		void renderToFbo(GTR::Scene* scene, GTR::LightEntity* light);
		//void renderToFbo(GTR::Scene* scene, Camera* camera);

	};

	Texture* CubemapFromHDRE(const char* filename);

};