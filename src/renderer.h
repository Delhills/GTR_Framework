#pragma once
#include "prefab.h"
#include "fbo.h"

#include "sphericalharmonics.h"

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


	enum eBlendMode {
		DITHERING,
		FORWARD_BLEND
	};

	class Prefab;
	class Material;

	struct renderCall { //recuerda añadir el tipo (prefab, ...)
		Matrix44 model;
		Mesh* mesh;
		Material* material;
		float distance_to_cam;
	};


	class SSAOFX {
	public:
		float intensity;

		std::vector<Vector3> points;

		SSAOFX();

		void blurTexture(Texture* input, Texture* output);
		void apply(Texture* depth_buffer, Texture* normal_buffer, Camera* cam, Texture* output);
	};

	//struct to store probes
	struct sProbe {
		Vector3 pos; //where is located
		Vector3 local; //its ijk pos in the matrix
		int index; //its index in the linear array
		SphericalHarmonics sh; //coeffs
	};


	std::vector<Vector3> generateSpherePoints(int num, float radius, bool hemi);
	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{

	public:

		//add here your functions
		//...
		FBO fbo;
		FBO gbuffers_fbo;
		FBO final_fbo;
		FBO* irr_fbo;

		bool rendering_shadowmap;

		eRenderMode render_mode;
		ePipelineMode pipeline_mode;
		eBlendMode blend_mode;

		eLightType light_types[5];

		Vector3 light_position[5];
		Vector3 light_target[5];
		Vector3 light_color[5];

		float light_intensity[5];
		float light_maxdists[5];
		float light_coscutoff[5];
		float light_spotexponent[5];

		Vector3 light_vector[5];

		bool hdr = true;
		bool show_fbo = false;
		bool show_gbuffers = false;
		bool showCameraDirectional = false;
		bool show_ao_buffer = false;
		bool show_depthfbo = false;
		bool apply_ssao = true;
		bool show_irr_fbo = false;

		float average_lum;
		float lum_white;
		float scale_tm;

		Texture* ao_buffer = NULL;
		Texture* ao_blur_buffer = NULL;

		SSAOFX ssao;

		Renderer();

		std::vector<renderCall> renderCallList;
		//renders several elements of the scene

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

		void renderGBuffers(GTR::Scene* scene, std::vector <renderCall>& rendercalls, Camera* camera);

		void renderToFbo(GTR::Scene* scene, GTR::LightEntity* light);
		//void renderToFbo(GTR::Scene* scene, Camera* camera);
		void renderInMenu();

		void renderProbe(Vector3 pos, float size, float* coeffs);
		void extractProbe(GTR::Scene* scene, sProbe& p);

		void updateIrradianceCache(GTR::Scene* scene);


		void view_gbuffers(FBO* gbuffers_fbo, float w, float h, Camera* camera);
		void renderFinalFBO(FBO* gbuffers_fbo, Camera* camera, GTR::Scene* scene, bool hdr, Texture* ao_buffer, std::vector <renderCall>& rendercalls);
		void setUniformsLight(LightEntity* light, Camera* camera, GTR::Scene* scene, Texture* ao_buffer, Shader* shader, bool hdr, FBO* gbuffers_fbo, bool first_iter);

	};

	Texture* CubemapFromHDRE(const char* filename);

};