// Simple basecode showing how to create a window and attatch a vulkan surface
#define GATEWARE_ENABLE_CORE // All libraries need this
#define GATEWARE_ENABLE_SYSTEM // Graphics libs require system level libraries
#define GATEWARE_ENABLE_GRAPHICS // Enables all Graphics Libraries
#define GATEWARE_ENABLE_MATH // Enables the Math library, used by the rendere
#define GATEWARE_ENABLE_INPUT // Enable input functionality for keyboard, mouse, and controllers

// Ignore some graphics libraries we aren't going to use
#define GATEWARE_DISABLE_GDIRECTX11SURFACE // we have another template for this
#define GATEWARE_DISABLE_GDIRECTX12SURFACE // we have another template for this
#define GATEWARE_DISABLE_GRASTERSURFACE // we have another template for this
#define GATEWARE_DISABLE_GOPENGLSURFACE // we have another template for this

// With what we want & what we don't defined we can include the API
#include "Gateware.h"
#include "FileIntoString.h"
#include "renderer.h"

// open some namespaces to compact the code a bit
using namespace GW;
using namespace CORE;
using namespace SYSTEM;
using namespace GRAPHICS;
using namespace MATH;
using namespace INPUT;

// lets pop a window and use Vulkan to clear to a red screen
int main()
{
	GWindow win;
	GEventResponder msgs;
	GVulkanSurface vulkan;

	if (+win.Create(0, 0, 800, 600, GWindowStyle::WINDOWEDBORDERED))
	{

		win.SetWindowName("Joshua Tomasso - Lab 6 - SECTION D - Complete"); 
		VkClearValue clrAndDepth[2];
		clrAndDepth[0].color = { {0.15f, 0.15f, 0.15f, 1} };
		clrAndDepth[1].depthStencil = { 1.0f, 0u };
		msgs.Create([&](const GW::GEvent& e) {
			GW::SYSTEM::GWindow::Events q;
			if (+e.Read(q) && q == GWindow::Events::RESIZE)
				clrAndDepth[0].color.float32[2] += 0.0f; 
			});
		win.Register(msgs);
#ifndef NDEBUG
		const char* debugLayers[] = {
			"VK_LAYER_KHRONOS_validation", // standard validation layer
		};
		const char* setting_debug_action[] = { // debug actions for the validation layer
			"VK_DBG_LAYER_ACTION_LOG_MSG",
			"VK_DBG_LAYER_ACTION_DEBUG_OUTPUT",
			"VK_DBG_LAYER_ACTION_BREAK"
		};
		const VkLayerSettingEXT lyr_sett = { // extended settings for the validation layer
			debugLayers[0], "debug_action", VK_LAYER_SETTING_TYPE_STRING_EXT,
			static_cast<uint32_t>(std::size(setting_debug_action)), setting_debug_action
		};
		if (+vulkan.Create(win, GW::GRAPHICS::DEPTH_BUFFER_SUPPORT | GW::GRAPHICS::BINDLESS_SUPPORT,
			sizeof(debugLayers) / sizeof(debugLayers[0]),
			debugLayers, 1, &lyr_sett, 0, nullptr, 0, nullptr, false))
#else
		if (+vulkan.Create(win, GW::GRAPHICS::DEPTH_BUFFER_SUPPORT | GW::GRAPHICS::BINDLESS_SUPPORT))
#endif
		{
			Renderer renderer(win, vulkan);
			while (+win.ProcessWindowEvents())
			{
				if (+vulkan.StartFrame(2, clrAndDepth))
				{
					renderer.Render();
					vulkan.EndFrame(true);
				}
			}
		}
	}
	return 0; // that's all folks
}
