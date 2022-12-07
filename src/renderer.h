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

	enum eFxMode {
		AA,
		BLUR,
		TRESHOLD,
		BLOOM,
		DOF
	};

	enum eBlendMode {
		DITHERING,
		FORWARD_BLEND
	};

	class Prefab;
	class Material;

	struct renderCall { //recuerda a�adir el tipo (prefab, ...)
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

	class FX {
	public:

		int w;
		int h;

		float treshold_intensity;
		float bloom_intensity;
		float min_distance;
		float max_distance;
		float focal_dist;

		bool horizontal = false;

		FX();

		void treshold(Texture* input, Texture* output);
		void blur(Texture* input, Texture* output);
		void bloom(Texture* input_base, Texture* input_blurred, Texture* output);
		void aa(Texture* input, Texture* output);
		void dof(Texture* input, Texture* input_blurred, Texture* depth_buffer, Camera* camera, Texture* output);
		void setFX(eFxMode fx, Texture* input, Texture* output, Texture* second_input = NULL, Texture* depth_buffer = NULL, Camera* camera = NULL);
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

		int w;
		int h;
		//add here your functions
		//...
		FBO fbo;
		FBO gbuffers_fbo;
		FBO decals_fbo;
		FBO* irr_fbo;

		bool use_only_FXAA = false;
		bool use_bloom_dof = true;
		bool use_volumetric = true;
		bool use_reflections = true;
		bool updateIrradianceOnce = true;
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
		bool apply_irr = true;
		bool apply_tri_irr = true;
		bool show_probes_text = false;
		bool render_probes = false;

		float average_lum;
		float lum_white;
		float scale_tm;

		Texture* ao_buffer = NULL;
		Texture* ao_blur_buffer = NULL;
		Texture* probes_texture = NULL;

		Texture* fx_blur_buffer = NULL;
		Texture* fx_threshold_buffer = NULL;
		Texture* fx_bloom_buffer = NULL;
		Texture* fx_aa_buffer = NULL;
		Texture* fx_dof_buffer = NULL;
		Texture* fx_dof_blurred_buffer = NULL;
		Texture* fx_dof_blurred_buffer_2 = NULL;

		SSAOFX ssao;

		FX fx;

		Matrix44 previous_vp;

		std::vector<sProbe> probes;

		Vector3 irr_start_pos;
		Vector3 irr_end_pos;
		Vector3 irr_dim;
		Vector3 irr_delta;
		float irr_normal_dist;

		Renderer();

		std::vector<renderCall> renderCallList;
		//renders several elements of the scene

		void render(GTR::Scene* scene, Camera* camera);

		void renderFinalFBO(GTR::Scene* scene, Camera* camera);

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

		void defineAndPosGridProbe(GTR::Scene* scene);

		void renderProbe(Vector3 pos, float size, float* coeffs);
		void extractProbe(GTR::Scene* scene, sProbe& p);

		void updateIrradianceCache(GTR::Scene* scene);

		void createIrradianceMap();
		void computeVolumetric(Camera* camera, Texture* depth_texture, Scene* scene);

		void renderDecalls(GTR::Scene* scene, Camera* camera);

		void renderSkybox(Texture* skybox, Camera* camera);

		void view_gbuffers(Camera* camera);
		void renderFinalFBO(FBO* gbuffers_fbo, Camera* camera, GTR::Scene* scene, bool hdr, Texture* ao_buffer, std::vector <renderCall>& rendercalls);
		void setUniformsLight(LightEntity* light, Camera* camera, GTR::Scene* scene, Texture* ao_buffer, Shader* shader, bool hdr, FBO* gbuffers_fbo, bool first_iter);

	};

	Texture* CubemapFromHDRE(const char* filename);

};
