
// [COMBO] {"material":"ui_editor_properties_blend_mode","combo":"BLENDMODE","type":"imageblending","default":30}

#include "common_blending.h"

varying vec4 v_TexCoord;

uniform sampler2D g_Texture0; // {"material":"framebuffer", "label":"ui_editor_properties_framebuffer", "hidden":true}
uniform sampler2D g_Texture1; // {"combo":"MASK","default":"util/white","label":"ui_editor_properties_opacity_mask","material":"mask","mode":"opacitymask","paintdefaultcolor":"0 0 0 1"}

uniform float g_BlendAlpha; // {"material":"alpha", "label":"ui_editor_properties_alpha","default":1,"range":[0,1]}
uniform vec3 g_TintColor; // {"default":"1 0 0","label":"ui_editor_properties_color","material":"color","type":"color"}

void main() {
	vec4 albedo = texSample2D(g_Texture0, v_TexCoord.xy);
	float mask = g_BlendAlpha;
	
#if MASK
	mask = texSample2D(g_Texture1, v_TexCoord.zw).r;
#endif
	
	albedo.rgb = ApplyBlending(BLENDMODE, albedo.rgb, g_TintColor, mask);
	
#if BLENDMODE == 0
	albedo.a = 1.0;
#endif
	
	gl_FragColor = albedo;
}
