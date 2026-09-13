#include <string_view>

int main()
{
    constexpr std::string_view name = "z-pilot";
    return name.empty() ? 1 : 0;
}
