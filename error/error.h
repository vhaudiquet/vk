void _fatal_kernel_error(const char* error_message, const char* error_context, char* err_file, u32 err_line);
#define fatal_kernel_error(em, ec) _fatal_kernel_error(em, ec, __FILE__, __LINE__)