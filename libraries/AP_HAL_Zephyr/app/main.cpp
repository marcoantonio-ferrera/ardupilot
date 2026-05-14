#ifdef __cplusplus
extern "C" {
#endif

int ardupilot_entry(int argc, char* const* argv);

#ifdef CONFIG_BOOTARGS
int main(int argc, char** argv)
{
    return ardupilot_entry(argc, (char* const*)argv);
}
#else
int main(void)
{
    return ardupilot_entry(0, nullptr);
}
#endif

#ifdef __cplusplus
}
#endif
