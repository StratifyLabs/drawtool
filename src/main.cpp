#include <stdio.h>

#include <sapi/sys.hpp>
#include <sapi/hal.hpp>
#include <sapi/var.hpp>
#include <sapi/draw.hpp>

#include "sl_config.h"

static void show_usage(const Cli & cli);
static bool is_memory_ok(const ConstString & application_path, const DisplayDevice & device);
static bool draw_scene(const ConstString & source, Display & display, Printer & printer);

int main(int argc, char * argv[]){
	Cli cli(argc, argv);
	cli.set_publisher(SL_CONFIG_PUBLISHER);
	cli.handle_version();

	String source;
	String device;
	String is_help;
	String is_stdout;

	Printer printer;

	source = cli.get_option("source", "specify the source JSON file to use for drawing");
	device = cli.get_option("device", "display device (default is /dev/display0)");
	is_help = cli.get_option("help", "show help");
	is_stdout = cli.get_option("stdout", "show the output on the standard output");

	if( is_help.is_empty() == false ){
		cli.show_options();
		exit(0);
	}

	DisplayDevice display;
	if( device.is_empty() ){
		device = "/dev/display0";
	}

	if( display.open(device, DisplayDevice::READWRITE) < 0 ){
		printer.error("failed to open the display device");
		exit(1);
	}

	if( is_memory_ok(cli.path(), display) == false ){
		printer.error("application does not have enough memory for display");
		exit(1);
	}

	if( display.initialize(device) < 0 ){
		printf("Failed to initialize display (%d, %d)\n",
				 display.return_value(),
				 display.error_number());
	}

	if( display.to_void() == 0 ){
		DisplayInfo display_info;
		display_info = display.get_info();
		printf("Not enough memory %dx%d %dbpp\n", display_info.width(), display_info.height(), display_info.bits_per_pixel());
		printf("Display needs %d bytes\n", (display.width()*display.height()*display.bits_per_pixel())/8);
		TaskInfo info = TaskManager::get_info();
		printf("Application has %ld bytes\n", info.memory_size());
		exit(1);
	}

	Timer t;

	//display.set_pen_color(0);
	display.draw_rectangle(Point(0,0), display.area());
	printer.open_object("display") << display.area() << printer.close();

	//display.set_pen_color(15);
	t.restart();
	if( draw_scene(source, display, printer) == false){
		show_usage(cli);
	}
	t.stop();

	printer.key("render time", F32U, t.microseconds());
	printer.key("size", "%ld", ((Bitmap&)display).size());
	printer.key("bmap", "%p", display.bmap());
	printer.key("bpp", "%d", display.bits_per_pixel());

	t.restart();
	display.write(display.bmap(), sizeof(sg_bmap_t));
	t.stop();
	printer.key("write time", F32U, t.microseconds());

	if( is_stdout == "true" ){
		printer << display;
	}

	display.close();

	printer.info("done");

	return 0;
}

bool is_memory_ok(const ConstString & application_path, const DisplayDevice & device){
	bool result = false;

	DisplayInfo display_info;
	AppfsInfo appfs_info;

	display_info = device.get_info();
	if( display_info.is_valid() ){
		appfs_info = Appfs::get_info(application_path);
		if( appfs_info.ram_size() > display_info.memory_size() + 1024 ){
			result = true;
		}
	}
	return result;
}

void show_usage(const Cli & cli){
	printf("%s usage:\n", cli.name().cstring());
	cli.show_options();
	exit(1);
}

bool draw_scene(const ConstString & source, Display & display, Printer & printer){
	JsonDocument document;
	JsonArray array = document.load_from_file(source).to_array();

	if( array.is_empty() ){
		printer.error("failed to load array of objects");
	}

	DrawingAttributes drawing_attributes(display, DrawingRegion(DrawingPoint::origin(), DrawingArea::maximum()));

	printer.open_object("scene");
	printer.open_object("region") << drawing_attributes.region() << printer.close();

	drawing_attributes.bitmap().clear();
	for(u32 i=0; i < array.count(); i++){
		//render each object
		JsonObject object = array.at(i).to_object();
		Timer t;

		//region applies to all object types
		DrawingRegion region;
		region << DrawingPoint(object.at("x").to_integer(), object.at("y").to_integer());
		region << DrawingArea(object.at("width").to_integer(), object.at("height").to_integer());
		sg_color_t color = object.at("color").to_integer();
		String class_value = object.at("class").to_string();

		printer.open_object(String().format("[%d]", i));
		printer.key("class", class_value);
		printer.open_object("region") << region << printer.close();
		printer.key("color", "%ld", color);

		if( class_value == "Rectangle" ){
			t.start();
			Rectangle().set_color(color).draw(drawing_attributes + region);
			t.stop();
		} else if( class_value == "RoundedRectangle" ){
			u8 radius = object.at("radius").to_integer();
			printer.key("radius", "%d", radius);
			t.start();
			RoundedRectangle().set_radius(radius).set_color(color).draw(drawing_attributes + region);
			t.stop();
		} else if( class_value == "BarProgress" ){
			u16 value = object.at("value").to_integer();
			u16 maximum = object.at("maximum").to_integer();
			sg_color_t background_color = object.at("backgroundColor").to_integer();
			u8 border_thickness = object.at("borderThickness").to_integer();
			printer.key("value", "%d", value);
			printer.key("maximum", "%d", maximum);
			printer.key("backgroundColor", "%d", background_color);
			printer.key("borderThickness", "%d", border_thickness);
			t.start();
			BarProgress()
					.set_progress(value, maximum)
					.set_border_thickness(border_thickness)
					.set_background_color(background_color)
					.set_color(color)
					.draw(drawing_attributes + region);
			t.stop();
		}

		printer.key("renderMicroseconds", "%ld", t.microseconds());
		printer.close_object();

	}

	printer.close_object();

	return true;

}




