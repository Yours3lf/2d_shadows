#version 430
//actually we could go with more lights, you just need to make sure you're working on them in groups of 64
//because that is the max that fits into the LDS
#define NUM_LIGHTS 64
layout(local_size_x = 16, local_size_y = 16) in; //local workgroup size

layout(location=0, rgba8) uniform image2D result;

layout(binding=1) uniform sampler2D texture0; //"occluder" map

//x,y: light pos (2D), z: light attenuation end, w: unused
layout(location=2) uniform vec4 light_posrad[NUM_LIGHTS];
//x,y,z: light color, w: intensity
layout(location=66) uniform vec4 light_colors[NUM_LIGHTS];
layout(location=130) uniform uint num_lights;

//max 24 lights per tile
#define MAX_TILE_LIGHTS 24
shared uint local_light_shadows_1D[MAX_TILE_LIGHTS * 256]; //4(bytes) * 24(lights) * 256(angles) = 24KB
shared int local_lights[MAX_TILE_LIGHTS];
shared int local_num_of_lights;

const float pi = 3.14159265;

vec3 tonemap_func(vec3 x, float a, float b, float c, float d, float e, float f)
{
  return ( ( x * ( a * x + c * b ) + d * e ) / ( x * ( a * x + b ) + d * f ) ) - e / f;
}

vec3 tonemap(vec3 col)
{
  //vec3 x = max( vec3(0), col - vec3(0.004));
  //return ( x * (6.2 * x + 0.5) ) / ( x * ( 6.2 * x + 1.7 ) + 0.06 );

  float a = 0.22; //Shoulder Strength
  float b = 0.30; //Linear Strength
  float c = 0.10; //Linear Angle
  float d = 0.20; //Toe Strength
  float e = 0.01; //Toe Numerator
  float f = 0.30; //Toe Denominator
  float linear_white = 11.2; //Linear White Point Value (11.2)
  //Note: E/F = Toe Angle

  return tonemap_func( col, a, b, c, d, e, f ) / tonemap_func( vec3(linear_white), a, b, c, d, e, f );
}

vec3 gamma_correct(vec3 col, float gamma)
{
	return pow(col, vec3(gamma));
}

float get_shadow( uint index, vec2 pos, vec2 light_pos )
{
  vec2 light_dir = light_pos - pos;
  float dist = length(light_dir);
  light_dir = normalize( light_dir );
  
  float angle = -light_dir.x; //dot( light_dir, vec2( 1, 0 ) );
  angle = angle * 0.5 + 0.5; //[0...1]
  angle *= 255; //[0...256]
  
  int sample_idx = clamp( int(angle) + int(index) * 256, 0, 24 * 256 - 1 );
  
  return uintBitsToFloat(local_light_shadows_1D[sample_idx]);
}

vec3 brdf(uint index, vec2 pos, vec2 light_pos, float light_radius, vec3 light_color, float light_intensity)
{
  vec2 light_dir = light_pos - pos;
  float dist = length(light_dir);
  
  float attenuation = max(light_radius - dist, 0) / light_radius;
  
  float shadow = float(dist < get_shadow( index, pos, light_pos ));
  
  return attenuation * light_color * light_intensity * shadow;
}

/*bool intersect_aabb_circle(vec2 aabb_min, vec2 aabb_max, vec2 aabb_center, vec2 circle_center, float circle_radius)
{
  vec2 vec = circle_center - clamp( circle_center - aabb_center, aabb_min, aabb_max );
  
  float sqlength = dot( vec, vec );
  
  if( sqlength > circle_radius * circle_radius )
    return false;
    
  return true;
}*/

void main()
{
  ivec2 global_id = ivec2( gl_GlobalInvocationID.xy );
	vec2 global_size = textureSize( texture0, 0 ).xy;
	ivec2 local_id = ivec2( gl_LocalInvocationID.xy );
	ivec2 local_size = ivec2( gl_WorkGroupSize.xy );
	ivec2 group_id = ivec2( gl_WorkGroupID.xy );
  ivec2 group_size = ivec2( gl_NumWorkGroups.xy );
	uint workgroup_index = gl_LocalInvocationIndex;
  //vec2 aspect = vec2(global_size.x/global_size.y, 1);
	vec2 texel = global_id / global_size;
  
  vec4 color = vec4(0);
  color.w = 1;
  
  float occluder = texture(texture0, texel).x; //0 --> occluded, 1 --> not occluded
  
  vec2 tile_min = group_id * local_size;
  vec2 tile_max = tile_min + local_size;
  vec2 tile_extents = local_size * 0.5; //half extents
  vec2 tile_center = tile_min + tile_extents;
  
  if( workgroup_index == 0 )
	{
		local_num_of_lights = 0;
	}
  
  barrier();
  
  //cull each light
  for( uint c = workgroup_index; c < num_lights; c += local_size.x * local_size.y )
  {
    vec2 light_pos = light_posrad[c].xy;
    float light_radius = light_posrad[c].z;
    
    vec2 light_min = light_pos - light_radius;
    vec2 light_max = light_pos + light_radius;
    vec2 light_extents = vec2(light_radius);
    
    vec2 t = abs( light_pos - tile_center );
    vec2 te = light_extents + tile_extents;
    
    //for some reason aabb vs circle doesn't work...
    //meh let's just use aabb vs aabb...
    //if( intersect_aabb_circle( tile_min, tile_max, tile_center, light_pos, light_radius ) )
    if( t.x <= te.x && t.y <= te.y )
    {
      int index = atomicAdd( local_num_of_lights, 1 );
      local_lights[index] = int(c);
      
      //for( int d = 0; d < 256; ++d ) //init 1D shadow map
      //  local_light_shadows_1D[d + index * 256] = 0x7f7fffff; // max float value
    }
  }
  
  barrier();
  
  //calculate shadows
  //for all the 256 angles
  //for( uint x = workgroup_index; x < 256; x += local_size.x * local_size.y )
  //{    
    //for all the lights
    for( uint c = 0; c < local_num_of_lights; ++c )
    {
      int index = local_lights[c];
      uint sample_idx = clamp( workgroup_index + c * 256, 0, 24 * 256 - 1 );
      float angle = 2 * pi * workgroup_index / 256.0;
    
      vec2 light_pos = light_posrad[index].xy;
      float light_radius = light_posrad[index].z;
    
      bool found = false;
    
      //raymarch along the direction
      for( uint y = 0; y < 256; ++y )
      {
        float r = light_radius * y / 256.0;
        vec2 sample_pos = (light_pos - r * vec2(cos(angle), sin(angle))) / global_size;
        
        float occ = texture(texture0, sample_pos).x;
        
        if( occ < 1.0 )
        {        
          //float dist = length(light_pos - sample_pos * global_size);
          
          //atomicMin( local_light_shadows_1D[sample_idx], floatBitsToUint(dist) );
          local_light_shadows_1D[sample_idx] = floatBitsToUint(r);
          
          found = true;
          break;
        }
      }
      
      if( !found )
        local_light_shadows_1D[sample_idx] = 0x7f7fffff; // max float value
    }
  //}
  
  barrier();
  
  //light each pixel
  for( uint c = 0; c < local_num_of_lights; ++c )
	{
    int index = local_lights[c];
    
    vec2 light_pos = light_posrad[index].xy;
    float light_radius = light_posrad[index].z;
    
    vec3 light_color = light_colors[index].xyz;
    float light_intensity = light_colors[index].w;
    
    color.xyz += brdf(c, global_id, light_pos, light_radius, light_color, light_intensity);
    
    /**/
    //debugging
    if( length( global_id - light_pos ) < 2 )
      color.xyz += vec3(1, 0, 0);
      
    if( length( global_id - light_pos ) < light_radius + 1 &&
        length( global_id - light_pos ) > light_radius - 1 )
      color.xyz += vec3(0, 1, 0);
    /**/
  }
  
  /**/
  //debugging
  if( local_id.x == 0 || 
      local_id.y == 0 )
  {
    color.xyz += vec3(0, 0, 1);
  }
  /**/
  
  color.xyz = gamma_correct( tonemap( color.xyz * occluder ), 1/2.2 );
  
  //debugging
  //color.xyz = color.xyz * 0.001 + vec3(local_num_of_lights / 24.0);
  //color.xyz = color.xyz * 0.001 + vec3(workgroup_index / 256.0);
  
  imageStore(result, global_id, color );
}