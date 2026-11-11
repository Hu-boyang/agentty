// 目标 -static 链接's --defsym 费用 __register_frame_info 财务报告和已审计财务报表
// __deregister_frame_info (见A/C.5/61/L.48,第16段) CMakeLists.txt。例外仍然未变 :
// libgcc通过PT_GNU_EH_FRAME通过dl_iterate_phdr找到FDes.
extern "C" void* agentty_skip_frame_registration(const void*, void*) { return nullptr; }
