#include "Mode.hpp"
#include "GL.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;
	void draw_text(std::string const &text, float x, float y, glm::uvec2 const &drawable_size, glm::vec3 color);


	struct Glyph {
		float u0, v0, u1, v1;
		int w, h;
		int bearing_x, bearing_y;
		int advance;
	};

	std::map<char, Glyph> glyphs;
	GLuint atlas_tex = 0;
	int atlas_w = 0;
	int atlas_h = 0;

	GLuint glyph_tex = 0;
	int glyph_w = 0;
	int glyph_h = 0;
	GLuint text_program = 0;
	GLuint text_vao = 0;
	GLuint text_vbo = 0;

	FT_Face ft_face = nullptr;
	hb_font_t *hb_font = nullptr;

	struct Choice {
		std::string label;
		std::string target;
	};

	struct Passage {
		std::string name;
		std::string prose;
		std::vector<Choice> choices;
	};

	std::map<std::string, Passage> passages;
	std::string current_passage;
	std::vector<std::string> history;

	void goto_passage(std::string const &name);
	float measure_text(std::string const &text);
	std::vector<std::string> wrap_text(std::string const &text, float max_width);

	void draw_text_centered(std::string const &text, float y, glm::uvec2 const &drawable_size, glm::vec3 color);
};