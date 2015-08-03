#include "framework.h"

using namespace prototyper;

float get_random_num( float min, float max )
{
  return min + ( max - min ) * ( float )rand() / ( float )RAND_MAX; //min...max
}

int main( int argc, char** argv )
{
  srand(time(0));

  map<string, string> args;

  for( int c = 1; c < argc; ++c )
  {
    args[argv[c]] = c + 1 < argc ? argv[c + 1] : "";
    ++c;
  }

  cout << "Arguments: " << endl;
  for_each( args.begin(), args.end(), []( pair<string, string> p )
  {
    cout << p.first << " " << p.second << endl;
  } );

  uvec2 screen( 0 );
  bool fullscreen = false;
  bool silent = false;
  string title = "Deferred 2D lighting and (massive # of smooth) shadows";

  /*
   * Process program arguments
   */

  stringstream ss;
  ss.str( args["--screenx"] );
  ss >> screen.x;
  ss.clear();
  ss.str( args["--screeny"] );
  ss >> screen.y;
  ss.clear();

  if( screen.x == 0 )
  {
    screen.x = 1280;
  }

  if( screen.y == 0 )
  {
    screen.y = 720;
  }

  try
  {
    args.at( "--fullscreen" );
    fullscreen = true;
  }
  catch( ... ) {}

  try
  {
    args.at( "--help" );
    cout << title << ", written by Marton Tamas." << endl <<
         "Usage: --silent      //don't display FPS info in the terminal" << endl <<
         "       --screenx num //set screen width (default:1280)" << endl <<
         "       --screeny num //set screen height (default:720)" << endl <<
         "       --fullscreen  //set fullscreen, windowed by default" << endl <<
         "       --help        //display this information" << endl;
    return 0;
  }
  catch( ... ) {}

  try
  {
    args.at( "--silent" );
    silent = true;
  }
  catch( ... ) {}

  /*
   * Initialize the OpenGL context
   */

  framework frm;
  frm.init( screen, title, fullscreen );
  
  //set opengl settings
  glEnable( GL_DEPTH_TEST );
  glDepthFunc( GL_LEQUAL );
  glFrontFace( GL_CCW );
  glEnable( GL_CULL_FACE );
  glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
  glClearDepth( 1.0f );

  frm.get_opengl_error();

  /*
   * Set up mymath
   */

  camera<float> cam;
  frame<float> the_frame;

  float cam_fov = 45.0f;
  float cam_near = 1.0f;
  float cam_far = 100.0f;

  the_frame.set_perspective( radians( cam_fov ), ( float )screen.x / ( float )screen.y, cam_near, cam_far );

  glViewport( 0, 0, screen.x, screen.y );

  /*
   * Set up the scene
   */

  //create a quad so that we can draw the result to the screen
  GLuint quad = frm.create_quad( vec3(-1, -1, 0), vec3(1, -1, 0), vec3(-1, 1, 0), vec3(1, 1, 0) );

  //load occluder texture
  sf::Image im;
  im.loadFromFile("../resources/2d_shadows/occluder.png");

  GLuint occluder_texture = 0;
  glGenTextures( 1, &occluder_texture );

  glBindTexture( GL_TEXTURE_2D, occluder_texture );
  glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, im.getSize().x, im.getSize().y, 0, GL_RGBA, GL_UNSIGNED_BYTE, im.getPixelsPtr() );

  //result texture
  GLuint result_texture = 0;
  glGenTextures( 1, &result_texture );

  glBindTexture( GL_TEXTURE_2D, result_texture );
  glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, screen.x, screen.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0 );

  //create lights
  vector<vec4> light_colors;
  vector<vec4> light_posrad;

  /**/
  light_colors.resize(10);
  for( auto& c : light_colors )
  {
    c = vec4(get_random_num(0.01f, 1), get_random_num(0.01f, 1), get_random_num(0.01f, 1), get_random_num(0.5f, 1));
  }

  light_posrad.resize(10);
  for( auto& c : light_posrad )
  {
    //consider the screen as [0...1] instead of native pixels
    c = vec4(get_random_num(0, screen.x), get_random_num(0, screen.y), get_random_num(50.0f, 200.0f), 0);
  }
  /**/

  /**
  //testing
  light_posrad.resize(1);
  light_posrad[0].z = 200;
  light_colors.resize(1);
  light_colors[0] = vec4(get_random_num(0.01f, 1), get_random_num(0.01f, 1), get_random_num(0.01f, 1), get_random_num(0.5f, 1));
  /**/

  /*
   * Set up the shaders
   */

  GLuint fs_shader = 0;
  frm.load_shader( fs_shader, GL_VERTEX_SHADER, "../shaders/2d_shadows/fs_shader.vs" );
  frm.load_shader( fs_shader, GL_FRAGMENT_SHADER, "../shaders/2d_shadows/fs_shader.ps" );

  GLuint shadows_shader = 0;
  frm.load_shader( shadows_shader, GL_COMPUTE_SHADER, "../shaders/2d_shadows/2d_shadows.cs" );

  GLuint light_posrad_loc = glGetUniformLocation( shadows_shader, "light_posrad" );
  GLuint light_colors_loc = glGetUniformLocation( shadows_shader, "light_colors" );
  GLuint num_lights_loc = glGetUniformLocation( shadows_shader, "num_lights" );

  /*
   * Handle events
   */

  auto event_handler = [&]( const sf::Event & ev )
  {
    switch( ev.type )
    {
      case sf::Event::KeyPressed:
        {
          /*if( ev.key.code == sf::Keyboard::A )
          {
            cam.rotate_y( radians( cam_rotation_amount ) );
          }*/
        }
      case sf::Event::MouseMoved:
        {
          light_posrad[0].x = ev.mouseMove.x;
          light_posrad[0].y = screen.y - ev.mouseMove.y;
        }
      default:
        break;
    }
  };

  /*
   * Render
   */

  sf::Clock timer;
  timer.restart();

  frm.display( [&]
  {
    frm.handle_events( event_handler );

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    //render lights and shadows
    glUseProgram( shadows_shader );

    glBindImageTexture( 0, result_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8 );

    glActiveTexture( GL_TEXTURE1 );
    glBindTexture( GL_TEXTURE_2D, occluder_texture );

    glUniform4fv(light_posrad_loc, light_posrad.size(), &light_posrad[0].x);
    glUniform4fv(light_colors_loc, light_colors.size(), &light_colors[0].x);
    glUniform1ui(num_lights_loc, light_colors.size());

    {
      vec3 gws( screen.x, screen.y, 1 );
      vec3 lws( 16, 16, 1 );
      vec3 dps( max( gws / lws, 1 ) );
      ivec3 idps( dps.x, dps.y, dps.z );
      glDispatchCompute( idps.x, idps.y, idps.z );
    }

    glMemoryBarrier( GL_SHADER_IMAGE_ACCESS_BARRIER_BIT );

    //render the result
    glUseProgram( fs_shader );

    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, result_texture );

    glBindVertexArray( quad );
    glDrawElements( GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0 );

    frm.get_opengl_error();
  }, silent );

  return 0;
}
