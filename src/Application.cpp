#include "Application.hpp"

#include "Config.hpp"
#include "OctreeBuilder.hpp"
#include "Voxelizer.hpp"
#include <plog/Log.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_internal.h>
#include <tinyfiledialogs.h>

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
			   VkDebugUtilsMessageTypeFlagsEXT message_type,
			   const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
			   void *user_data) {
	if (message_severity >= VkDebugUtilsMessageSeverityFlagBitsEXT::
	VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		LOGE.printf("%s", callback_data->pMessage);
	else if (message_severity >=
			 VkDebugUtilsMessageSeverityFlagBitsEXT::
			 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		LOGW.printf("%s", callback_data->pMessage);
	else LOGI.printf("%s", callback_data->pMessage);
	return VK_FALSE;
}

void Application::create_window() {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	m_window = glfwCreateWindow(kWidth, kHeight, kAppName, nullptr, nullptr);
	glfwSetWindowUserPointer(m_window, this);
	glfwSetKeyCallback(m_window, glfw_key_callback);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsCinder();
	ImGui_ImplGlfw_InitForVulkan(m_window, true);
}

void Application::create_render_pass() {
	VkAttachmentDescription color_attachment = {};
	color_attachment.format = m_swapchain->GetImageFormat();
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	std::vector<VkSubpassDescription> subpasses(2);
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachmentCount = 1;
	subpasses[0].pColorAttachments = &color_attachment_ref;
	subpasses[0].pDepthStencilAttachment = nullptr;

	subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[1].colorAttachmentCount = 1;
	subpasses[1].pColorAttachments = &color_attachment_ref;
	subpasses[1].pDepthStencilAttachment = nullptr;

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = subpasses.size();
	render_pass_info.pSubpasses = subpasses.data();

	std::vector<VkSubpassDependency> subpass_dependencies(2);
	subpass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	subpass_dependencies[0].dstSubpass = 0;
	subpass_dependencies[0].srcStageMask =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpass_dependencies[0].dstStageMask =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpass_dependencies[0].srcAccessMask = 0;
	subpass_dependencies[0].dstAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	subpass_dependencies[1].srcSubpass = 0;
	subpass_dependencies[1].dstSubpass = 1;
	subpass_dependencies[1].srcStageMask =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpass_dependencies[1].dstStageMask =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpass_dependencies[1].srcAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpass_dependencies[1].dstAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	render_pass_info.dependencyCount = subpass_dependencies.size();
	render_pass_info.pDependencies = subpass_dependencies.data();

	m_render_pass = myvk::RenderPass::Create(m_device, render_pass_info);
}

void Application::create_framebuffers() {
	m_framebuffers.resize(m_swapchain->GetImageCount());
	for (uint32_t i = 0; i < m_swapchain->GetImageCount(); ++i) {
		m_framebuffers[i] = myvk::Framebuffer::Create(
			m_render_pass, {m_swapchain_image_views[i]},
			m_swapchain->GetExtent());
	}
}

void Application::draw_frame() {
	m_frame_manager.BeforeAcquire();
	uint32_t image_index;
	m_swapchain->AcquireNextImage(
		&image_index, m_frame_manager.GetAcquireDoneSemaphorePtr(), nullptr);
	m_frame_manager.AfterAcquire(image_index);

	uint32_t current_frame = m_frame_manager.GetCurrentFrame();
	m_camera.UpdateFrameUniformBuffer(current_frame);
	const std::shared_ptr<myvk::CommandBuffer> &command_buffer =
		m_frame_command_buffers[current_frame];

	command_buffer->Reset();
	command_buffer->Begin();
	if (!m_octree.Empty())
		m_octree_tracer.CmdBeamRenderPass(command_buffer, current_frame);
	command_buffer->CmdBeginRenderPass(m_render_pass,
									   m_framebuffers[image_index],
									   {{{0.0f, 0.0f, 0.0f, 1.0f}}});
	if (!m_octree.Empty())
		m_octree_tracer.CmdDrawPipeline(command_buffer, current_frame);
	command_buffer->CmdNextSubpass();
	m_imgui_renderer.CmdDrawPipeline(command_buffer, current_frame);
	command_buffer->CmdEndRenderPass();
	command_buffer->End();

	m_frame_manager.BeforeSubmit();
	command_buffer->Submit({{m_frame_manager.GetAcquireDoneSemaphorePtr(),
								VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT}},
						   {m_frame_manager.GetRenderDoneSemaphorePtr()},
						   m_frame_manager.GetFrameFencePtr());
	m_swapchain->Present(image_index,
						 {m_frame_manager.GetRenderDoneSemaphorePtr()});
}

void Application::initialize_vulkan() {
	m_instance = myvk::Instance::CreateWithGlfwExtensions(false, debug_callback);
	if (!m_instance) {
		LOGE.printf("Failed to create instance!");
		exit(EXIT_FAILURE);
	}

	std::vector<std::shared_ptr<myvk::PhysicalDevice>> physical_devices =
		myvk::PhysicalDevice::Fetch(m_instance);
	if (physical_devices.empty()) {
		LOGE.printf("Failed to find physical device with vulkan support!");
		exit(EXIT_FAILURE);
	}

	m_surface = myvk::Surface::Create(m_instance, m_window);
	if (!m_surface) {
		LOGE.printf("Failed to create surface!");
		exit(EXIT_FAILURE);
	}

	// DEVICE CREATION
	{
		std::vector<myvk::QueueRequirement> queue_requirements = {
			myvk::QueueRequirement(VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT,
								   &m_graphics_compute_queue, m_surface,
								   &m_present_queue),
			myvk::QueueRequirement(VK_QUEUE_COMPUTE_BIT,
								   &m_async_compute_queue)};
		myvk::DeviceCreateInfo device_create_info;
		device_create_info.Initialize(physical_devices[0], queue_requirements,
									  {VK_KHR_SWAPCHAIN_EXTENSION_NAME});
		if (!device_create_info.QueueSupport()) {
			LOGE.printf("Failed to find queues!");
			exit(EXIT_FAILURE);
		}
		if (!device_create_info.ExtensionSupport()) {
			LOGE.printf("Failed to find extension support!");
			exit(EXIT_FAILURE);
		}
		m_device = myvk::Device::Create(device_create_info);
		if (!m_device) {
			LOGE.printf("Failed to create logical device!");
			exit(EXIT_FAILURE);
		}
	}

	LOGI.printf("Physical Device: %s",
				m_device->GetPhysicalDevicePtr()->GetProperties().deviceName);
	LOGI.printf("Present Queue: %p, Graphics|Compute Queue: %p, Async Compute "
				"Queue: %p",
				m_present_queue->GetHandle(),
				m_graphics_compute_queue->GetHandle(),
				m_async_compute_queue->GetHandle());

	if (m_async_compute_queue->GetHandle() ==
		m_graphics_compute_queue->GetHandle()) {
		LOGE.printf(
				"No separate Compute Queue support, Path Tracer not available");
	} else if (m_async_compute_queue->GetFamilyIndex() ==
			   m_graphics_compute_queue->GetFamilyIndex()) {
		LOGW.printf("Async Compute Queue is not fully asynchronous");
	}

	m_swapchain = myvk::Swapchain::Create(m_graphics_compute_queue,
										  m_present_queue, false);
	LOGI.printf("Swapchain image count: %u", m_swapchain->GetImageCount());

	m_swapchain_images = myvk::SwapchainImage::Create(m_swapchain);
	m_swapchain_image_views.resize(m_swapchain->GetImageCount());
	for (uint32_t i = 0; i < m_swapchain->GetImageCount(); ++i)
		m_swapchain_image_views[i] =
			myvk::ImageView::Create(m_swapchain_images[i]);

	m_graphics_compute_command_pool = myvk::CommandPool::Create(
		m_graphics_compute_queue,
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	m_frame_command_buffers = myvk::CommandBuffer::CreateMultiple(
		m_graphics_compute_command_pool, kFrameCount);
}

Application::Application() {
	if (volkInitialize() != VK_SUCCESS) {
		LOGE.printf("Failed to load vulkan!");
		exit(EXIT_FAILURE);
	}

	create_window();
	initialize_vulkan();
	create_render_pass();
	create_framebuffers();
	m_camera.Initialize(m_device, kFrameCount);
	m_camera.m_position = glm::vec3(1.5);
	m_frame_manager.Initialize(m_swapchain, kFrameCount);
	m_octree.Initialize(m_device);
	m_octree_tracer.Initialize(m_octree, m_camera, m_render_pass, 0,
							   kFrameCount);
	m_imgui_renderer.Initialize(m_graphics_compute_command_pool, m_render_pass,
								1, kFrameCount);
}

void Application::LoadScene(const char *filename, uint32_t octree_level) {
	m_device->WaitIdle();
	Scene scene;
	if (scene.Initialize(m_graphics_compute_queue, filename)) {
		Voxelizer voxelizer;
		voxelizer.Initialize(scene, m_graphics_compute_command_pool,
							 octree_level);
		OctreeBuilder builder;
		builder.Initialize(voxelizer, m_graphics_compute_command_pool,
						   octree_level);

		std::shared_ptr<myvk::Fence> fence = myvk::Fence::Create(m_device);
		std::shared_ptr<myvk::QueryPool> query_pool = myvk::QueryPool::Create(m_device, VK_QUERY_TYPE_TIMESTAMP, 4);
		std::shared_ptr<myvk::CommandBuffer> command_buffer =
			myvk::CommandBuffer::Create(m_graphics_compute_command_pool);
		command_buffer->Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		command_buffer->CmdResetQueryPool(query_pool);

		command_buffer->CmdWriteTimestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, 0);
		voxelizer.CmdVoxelize(command_buffer);
		command_buffer->CmdWriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 1);

		command_buffer->CmdPipelineBarrier(
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {},
			{voxelizer.GetVoxelFragmentListPtr()->GetMemoryBarrier(
				VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)},
			{});

		command_buffer->CmdWriteTimestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, 2);
		builder.CmdBuild(command_buffer);
		command_buffer->CmdWriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 3);

		command_buffer->End();

		LOGV.printf("Voxelize and Octree building BEGIN");

		command_buffer->Submit({}, {}, fence);
		fence->Wait();

		//time measurement
		uint64_t timestamps[4];
		query_pool->GetResults64(timestamps, VK_QUERY_RESULT_WAIT_BIT);
		LOGV.printf("Voxelize and Octree building FINISHED in %lf ms (Voxelize %lf ms, Octree building %lf ms)",
					double(timestamps[3] - timestamps[0]) * 0.000001,
					double(timestamps[1] - timestamps[0]) * 0.000001,
					double(timestamps[3] - timestamps[2]) * 0.000001);

		m_octree.Update(
			builder.GetOctree(), octree_level,
			builder.GetOctreeRange(m_graphics_compute_command_pool));
		LOGV.printf("Octree range: %lu (%.1f MB)", m_octree.GetRange(),
					m_octree.GetRange() / 1000000.0f);
	}
}

void Application::Run() {
	double lst_time = glfwGetTime();
	while (!glfwWindowShouldClose(m_window)) {
		double cur_time = glfwGetTime();

		glfwPollEvents();

		m_camera.Control(m_window, float(cur_time - lst_time));

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		if (m_ui_display_flag)
			ui_main();
		ImGui::Render();

		draw_frame();
		lst_time = cur_time;
	}
	m_device->WaitIdle();
}

void Application::ui_main() {
	ui_main_menubar();
	ui_info_overlay();
}

void Application::ui_push_disable() {
	ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
}

void Application::ui_pop_disable() {
	ImGui::PopItemFlag();
	ImGui::PopStyleVar();
}

void Application::ui_info_overlay() {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y),
							ImGuiCond_Always, ImVec2(1.0f, 1.0f));
	ImGui::PushStyleColor(
		ImGuiCol_WindowBg,
		ImVec4(0.0f, 0.0f, 0.0f, 0.4f)); // Transparent background
	if (ImGui::Begin("INFO", nullptr,
					 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
					 ImGuiWindowFlags_AlwaysAutoResize |
					 ImGuiWindowFlags_NoMove |
					 ImGuiWindowFlags_NoSavedSettings |
					 ImGuiWindowFlags_NoBringToFrontOnFocus)) {
		// ImGui::Text("Toggle UI display with [X]");
		if (ImGui::TreeNodeEx("Basic", ImGuiTreeNodeFlags_DefaultOpen)) {
			uint32_t vulkan_version = volkGetInstanceVersion();
			ImGui::Text("Vulkan Version: %u.%u.%u",
						VK_VERSION_MAJOR(vulkan_version),
						VK_VERSION_MINOR(vulkan_version),
						VK_VERSION_PATCH(vulkan_version));
			ImGui::Text(
				"Physical Device: %s",
				m_device->GetPhysicalDevicePtr()->GetProperties().deviceName);
			ImGui::Text("Framerate: %f", ImGui::GetIO().Framerate);

			ImGui::TreePop();
			ImGui::Separator();
		}

		if (!m_octree.Empty() && ImGui::TreeNodeEx("Octree")) {
			ImGui::Text("Level: %d", m_octree.GetLevel());
			ImGui::Text("Allocated Size: %.1f MB",
						m_octree.GetBufferPtr()->GetSize() / 1000000.0f);
			ImGui::Text("Used Size: %.1f MB", m_octree.GetRange() / 1000000.0f);

			ImGui::TreePop();
		}

		/*if(m_pathtracing_flag)
		    ImGui::Text("SPP: %d", m_pathtracer.GetSPP());*/

		ImGui::End();
	}
	ImGui::PopStyleColor();
}

void Application::ui_main_menubar() {
	bool open_load_scene_popup = false, open_export_exr_popup = false;

	ImGui::BeginMainMenuBar();

	if (!m_pathtracing_flag) {
		if (ImGui::Button("Load Scene"))
			open_load_scene_popup = true;

		/*if(m_octree && ImGui::Button("Start PT"))
		{
		    m_pathtracing_flag = true;
		    m_pathtracer.Prepare(m_camera, *m_octree, m_octree_tracer);
		}*/

		if (ImGui::BeginMenu("Camera")) {
			ImGui::DragAngle("FOV", &m_camera.m_fov, 1, 10, 179);
			ImGui::DragFloat("Speed", &m_camera.m_speed, 0.005f, 0.005f, 0.2f);
			ImGui::InputFloat3("Position", &m_camera.m_position[0]);
			ImGui::DragAngle("Yaw", &m_camera.m_yaw, 1, 0, 360);
			ImGui::DragAngle("Pitch", &m_camera.m_pitch, 1, -90, 90);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Primary View")) {
			if (ImGui::MenuItem("Diffuse", nullptr, m_octree_tracer.m_view_type == OctreeTracer::ViewTypes::kDiffuse))
				m_octree_tracer.m_view_type = OctreeTracer::ViewTypes::kDiffuse;
			if (ImGui::MenuItem("Normal", nullptr, m_octree_tracer.m_view_type == OctreeTracer::ViewTypes::kNormal))
				m_octree_tracer.m_view_type = OctreeTracer::ViewTypes::kNormal;
			if (ImGui::MenuItem("Iterations", nullptr,
								m_octree_tracer.m_view_type == OctreeTracer::ViewTypes::kIteration))
				m_octree_tracer.m_view_type = OctreeTracer::ViewTypes::kIteration;

			ImGui::Checkbox("Beam Optimization",
							&m_octree_tracer.m_beam_enable);
			ImGui::EndMenu();
		}

		/*if(ImGui::BeginMenu("Path Tracer"))
		{
		    ImGui::DragInt("Bounce", &m_pathtracer.m_bounce, 1, 2, kMaxBounce);
		    ImGui::DragFloat3("Sun Radiance", &m_pathtracer.m_sun_radiance[0],
		0.1f, 0.0f, 20.0f); ImGui::EndMenu();
		}*/
	}
	/*else if(m_octree)
	{
	    if(ImGui::Button("Exit PT"))
	        m_pathtracing_flag = false;
	    if(ImGui::Button("Export OpenEXR"))
	        open_export_exr_popup = true;

	    ImGui::Checkbox("Pause", &m_pathtracer.m_pause);

	    if(ImGui::BeginMenu("View"))
	    {
	        if(ImGui::MenuItem("Color", nullptr, m_pathtracer.m_view_type ==
	PathTracer::kColor)) m_pathtracer.m_view_type = PathTracer::kColor;
	        if(ImGui::MenuItem("Albedo", nullptr, m_pathtracer.m_view_type ==
	PathTracer::kAlbedo)) m_pathtracer.m_view_type = PathTracer::kAlbedo;
	        if(ImGui::MenuItem("Normal", nullptr, m_pathtracer.m_view_type ==
	PathTracer::kNormal)) m_pathtracer.m_view_type = PathTracer::kNormal;

	        ImGui::EndMenu();
	    }
	}*/

	ImGui::EndMainMenuBar();

	if (open_load_scene_popup)
		ImGui::OpenPopup("Load Scene");
	// if (open_export_exr_popup)
	//	ImGui::OpenPopup("Export OpenEXR");

	ui_load_scene_modal();
	ui_export_exr_modal();
}

bool Application::ui_file_open(const char *label, const char *btn, char *buf,
							   size_t buf_size, const char *title,
							   int filter_num,
							   const char *const *filter_patterns) {
	bool ret = ImGui::InputText(label, buf, buf_size);
	ImGui::SameLine();

	if (ImGui::Button(btn)) {
		const char *filename = tinyfd_openFileDialog(
			title, "", filter_num, filter_patterns, nullptr, false);
		if (filename)
			strcpy(buf, filename);
		ret = true;
	}
	return ret;
}

bool Application::ui_file_save(const char *label, const char *btn, char *buf,
							   size_t buf_size, const char *title,
							   int filter_num,
							   const char *const *filter_patterns) {
	bool ret = ImGui::InputText(label, buf, buf_size);
	ImGui::SameLine();

	if (ImGui::Button(btn)) {
		const char *filename = tinyfd_saveFileDialog(title, "", filter_num,
													 filter_patterns, nullptr);
		if (filename)
			strcpy(buf, filename);
		ret = true;
	}
	return ret;
}

void Application::ui_load_scene_modal() {
	if (ImGui::BeginPopupModal("Load Scene", nullptr,
							   ImGuiWindowFlags_AlwaysAutoResize |
							   ImGuiWindowFlags_NoTitleBar |
							   ImGuiWindowFlags_NoMove)) {
		static char name_buf[kFilenameBufSize];
		static int octree_leve = 10;

		constexpr const char *kFilter[] = {"*.obj"};

		ui_file_open("OBJ Filename", "...##5", name_buf, kFilenameBufSize,
					 "OBJ Filename", 1, kFilter);
		ImGui::DragInt("Octree Level", &octree_leve, 1, 2, 12);

		if (ImGui::Button("Load", ImVec2(256, 0))) {
			LoadScene(name_buf, octree_leve);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(256, 0)))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}
}

void Application::ui_export_exr_modal() {
	/*if (ImGui::BeginPopupModal("Export OpenEXR", nullptr,
	                           ImGuiWindowFlags_AlwaysAutoResize |
	ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
	{
	    static char exr_name_buf[kFilenameBufSize]{};
	    static bool save_as_fp16{false};
	    ImGui::LabelText("", "INFO: will export %s channel",
	                     m_pathtracer.m_view_type == PathTracer::kColor ?
	"COLOR" : (m_pathtracer.m_view_type == PathTracer::kAlbedo ? "ALBEDO" :
	"NORMAL"));

	    constexpr const char *kFilter[] = {"*.exr"};
	    ui_file_save("OpenEXR Filename", "...##0", exr_name_buf,
	kFilenameBufSize, "Export OpenEXR", 1, kFilter);

	    ImGui::Checkbox("Export As FP16", &save_as_fp16);

	    {
	        if (ImGui::Button("Export", ImVec2(256, 0)))
	        {
	            m_pathtracer.Save(exr_name_buf, save_as_fp16);
	            ImGui::CloseCurrentPopup();
	        }
	        ImGui::SetItemDefaultFocus();
	        ImGui::SameLine();
	        if (ImGui::Button("Cancel", ImVec2(256, 0)))
	        {
	            ImGui::CloseCurrentPopup();
	        }
	    }

	    ImGui::EndPopup();
	}*/
}

void Application::glfw_key_callback(GLFWwindow *window, int key, int,
									int action, int) {
	auto *app = (Application *) glfwGetWindowUserPointer(window);
	if (!ImGui::GetCurrentContext()->NavWindow ||
		(ImGui::GetCurrentContext()->NavWindow->Flags &
		 ImGuiWindowFlags_NoBringToFrontOnFocus)) {
		if (action == GLFW_PRESS && key == GLFW_KEY_X)
			app->m_ui_display_flag ^= 1u;
	}
}
