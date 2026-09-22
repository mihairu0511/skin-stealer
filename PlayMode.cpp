#include "PlayMode.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "gl_compile_program.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include "json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
using json = nlohmann::json;

//References:
//https://learnopengl.com/In-Practice/Text-Rendering
//https://en.wikibooks.org/wiki/OpenGL_Programming/Modern_OpenGL_Tutorial_Text_Rendering_02
//https://learnopengl.com/Getting-started/Hello-Triangle
//https://github.com/harfbuzz/harfbuzz-tutorial/blob/master/hello-harfbuzz-freetype.c
//https://www.freetype.org/freetype2/docs/tutorial/step1.html
//https://github.com/nlohmann/json
//https://github.com/lazerwalker/twison
//https://fonts.google.com/specimen/Quicksand?preview.script=Latn

PlayMode::PlayMode() {
	//Reference: https://www.freetype.org/freetype2/docs/tutorial/step1.html
	FT_Library ft;
	FT_Init_FreeType(&ft);
	FT_Face face;
	if (FT_New_Face(ft, data_path("font.ttf").c_str(), 0, &face)) {
		throw std::runtime_error("Font load failed.");
	}
	FT_Set_Pixel_Sizes(face, 0, 48);

	int pen_x = 0;
	int max_h = 0;
	for (char c = 32; c < 127; ++c) {
		if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
			continue;
		}
		pen_x += face->glyph->bitmap.width + 1;
		max_h = std::max(max_h, int(face->glyph->bitmap.rows));
	}
	atlas_w = pen_x;
	atlas_h = max_h;

    // Reference: https://learnopengl.com/In-Practice/Text-Rendering
	// https://en.wikibooks.org/wiki/OpenGL_Programming/Modern_OpenGL_Tutorial_Text_Rendering_02

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glGenTextures(1, &atlas_tex);
	glBindTexture(GL_TEXTURE_2D, atlas_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlas_w, atlas_h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	pen_x = 0;

	for (char c = 32; c < 127; ++c) {
		if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
			continue;
		}
		FT_GlyphSlot g = face->glyph;
		if (g->bitmap.width > 0 && g->bitmap.rows > 0) {
			glTexSubImage2D(GL_TEXTURE_2D, 0, pen_x, 0, g->bitmap.width, g->bitmap.rows, GL_RED, GL_UNSIGNED_BYTE, g->bitmap.buffer);
		}

		Glyph gl;
		gl.u0 = float(pen_x) / atlas_w;
		gl.v0 = 0.0f;
		gl.u1 = float(pen_x + g->bitmap.width) / atlas_w;
		gl.v1 = float(g->bitmap.rows) / atlas_h;
		gl.w = g->bitmap.width;
		gl.h = g->bitmap.rows;
		gl.bearing_x = g->bitmap_left;
		gl.bearing_y = g->bitmap_top;
		gl.advance = g->advance.x >> 6;
		glyphs[c] = gl;
		pen_x += g->bitmap.width + 1;
	}
	glBindTexture(GL_TEXTURE_2D, 0);

	ft_face = face;
	hb_font = hb_ft_font_create(ft_face, nullptr);

	//Reference: https://learnopengl.com/In-Practice/Text-Rendering
	//https://learnopengl.com/Getting-started/Hello-Triangle

	text_program = gl_compile_program(
		"#version 330\n"
		"layout(location=0) in vec2 Position;\n"
		"layout(location=1) in vec2 TexCoord;\n"
		"out vec2 texCoord;\n"
		"void main() { gl_Position = vec4(Position,0.0,1.0); texCoord = TexCoord; }\n"
		,
		"#version 330\n"
		"in vec2 texCoord;\n"
		"out vec4 fragColor;\n"
		"uniform sampler2D Tex;\n"
		"uniform vec3 Color;\n"
		"void main() { fragColor = vec4(Color, texture(Tex, texCoord).r); }\n"
	);

	glGenVertexArrays(1, &text_vao);
	glBindVertexArray(text_vao);
	glGenBuffers(1, &text_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)(sizeof(float)*2));
	glEnableVertexAttribArray(1);
	glBindVertexArray(0);

	//Reference: https://github.com/nlohmann/json
	//https://github.com/lazerwalker/twison
	std::ifstream f(data_path("story.json"));
	if (!f) throw std::runtime_error("Could not open story.json");
	json story;
	f >> story;

	for (auto const &p : story["passages"]) {
		Passage passage;
		passage.name = p["name"];

		std::istringstream iss(p["text"].get<std::string>());
		std::string line;

		while (std::getline(iss, line)) {
			if (line.rfind("[[", 0) == 0) {
				continue;
			}
			if (!passage.prose.empty()) {
				passage.prose += "\n";
			}
			passage.prose += line;
		}

		if (p.contains("links")) {
			for (auto const &l : p["links"]) {
				passage.choices.push_back({ l["name"], l["link"] });
			}
		}
		passages[passage.name] = passage;
	}

	std::string start_pid = story["startnode"];
	for (auto const &p : story["passages"]) {
		if (p["pid"] == start_pid) { 
			current_passage = p["name"]; 
			break; 
		}
	}

	GL_ERRORS();
}

PlayMode::~PlayMode() {}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	(void) window_size;
	if (evt.type != SDL_EVENT_KEY_DOWN) {
		return false;
	}

	if (evt.key.key == SDLK_ESCAPE) {
		return true;
	}

	if (evt.key.key == SDLK_R) {
		if (!history.empty()) { 
			current_passage = history.back();
			history.pop_back();
		}

		return true;
	}

	int idx = -1;
	if (evt.key.key == SDLK_1) {
		idx = 0;
	} else if (evt.key.key == SDLK_2) {
		idx = 1;
	} else if (evt.key.key == SDLK_3) {
		idx = 2;
	} else if (evt.key.key == SDLK_4) {
		idx = 3;
	}

	auto it = passages.find(current_passage);
	if (idx >= 0 && it != passages.end() && idx < int(it->second.choices.size())) {
		goto_passage(it->second.choices[idx].target);
		return true;
	}

	return false;
}

void PlayMode::update(float elapsed) { 
	(void) elapsed; 
}

void PlayMode::draw_text(std::string const &text, float x, float y, glm::uvec2 const &drawable_size, glm::vec3 color) {
	glUseProgram(text_program);
	glUniform3f(glGetUniformLocation(text_program, "Color"), color.r, color.g, color.b);
	glUniform1i(glGetUniformLocation(text_program, "Tex"), 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, atlas_tex);
	glBindVertexArray(text_vao);
	glBindBuffer(GL_ARRAY_BUFFER, text_vbo);

	//Reference: https://github.com/harfbuzz/harfbuzz-tutorial/blob/master/hello-harfbuzz-freetype.c
	hb_buffer_t *buf = hb_buffer_create();
	hb_buffer_add_utf8(buf, text.c_str(), -1, 0, -1);
	hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
	hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
	hb_buffer_set_language(buf, hb_language_from_string("en", -1));
	hb_shape(hb_font, buf, nullptr, 0);

	unsigned int count = hb_buffer_get_length(buf);
	hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, nullptr);
	hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, nullptr);

	float pen_x = x;
	for (unsigned int i = 0; i < count; ++i) {
		char c = text[info[i].cluster];
		auto it = glyphs.find(c);
		if (it != glyphs.end()) {
			Glyph const &g = it->second;
			float gx = pen_x + g.bearing_x + pos[i].x_offset / 64.0f;
			float gy = y - g.bearing_y;
			float gw = float(g.w), gh = float(g.h);

			auto cx = [&](float px) { 
				return px / drawable_size.x * 2.0f - 1.0f; 
			};
			auto cy = [&](float py){
				return 1.0f - py / drawable_size.y * 2.0f;
			};

			float x0 = cx(gx);
			float x1 = cx(gx + gw);
			float y0 = cy(gy);
			float y1 = cy(gy + gh);

			float verts[6*4] = {x0, y0, g.u0, g.v0,  x0, y1, g.u0, g.v1,  x1, y1, g.u1, g.v1, x0, y0, g.u0, g.v0,  x1, y1, g.u1, g.v1,  x1, y0, g.u1, g.v0,};
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
		pen_x += pos[i].x_advance / 64.0f;
	}

	hb_buffer_destroy(buf);
	glBindVertexArray(0);
	glUseProgram(0);
}

float PlayMode::measure_text(std::string const &text) {
	float w = 0.0f;
	for (char c : text) {
		auto it = glyphs.find(c);
		if (it != glyphs.end()) {
			w += it->second.advance;
		}
	}
	return w;
}

std::vector<std::string> PlayMode::wrap_text(std::string const &text, float max_width) {
	std::vector<std::string> lines;
	std::istringstream iss(text);
	std::string word, line;
	while (iss >> word) {
		std::string test = line.empty() ? word : line + " " + word;
		if (measure_text(test) > max_width && !line.empty()) {
			lines.push_back(line);
			line = word;
		} else {
			line = test;
		}
	}
	if (!line.empty()) {
		lines.push_back(line);
	}
	return lines;
}

void PlayMode::draw_text_centered(std::string const &text, float y, glm::uvec2 const &drawable_size, glm::vec3 color) {
	draw_text(text, (drawable_size.x - measure_text(text)) * 0.5f, y, drawable_size, color);
}

void PlayMode::goto_passage(std::string const &name) {
	if (passages.count(name)) {
		history.push_back(current_passage);
		current_passage = name;
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	auto it = passages.find(current_passage);
	if (it != passages.end()) {
		Passage const &p = it->second;
		float y = drawable_size.y * 0.3f;

		for (std::string const &line : wrap_text(p.prose, drawable_size.x * 0.5f)) {
			draw_text_centered(line, y, drawable_size, glm::vec3(1.0f));
			y += 60.0f;
		}
		y += 60.0f;
		for (size_t i = 0; i < p.choices.size(); ++i) {
			draw_text_centered(std::to_string(i+1) + ") " + p.choices[i].label, y, drawable_size, glm::vec3(0.8f, 0.9f, 1.0f));
			y += 50.0f;
		}
	}

	if (!history.empty()) {
		draw_text_centered("(R) go back", drawable_size.y - 60.0f, drawable_size, glm::vec3(0.5f));
	}

	glDisable(GL_BLEND);
	GL_ERRORS();
}