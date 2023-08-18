#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/trace/default_span.h>
#include <iostream>

int main()
{
  // Test uses of OpenTelemetry API
  auto span = opentelemetry::trace::DefaultSpan::GetInvalid();
  std::cout << span.ToString() << "\n";

  // Test uses of OpenTelemetry SDK - Resource
  auto resource = opentelemetry::sdk::resource::Resource::GetDefault();
  for (auto const &kv : resource.GetAttributes())
  {
    auto s = opentelemetry::nostd::get<std::string>(kv.second);
    std::cout << kv.first << ": " << s << "\n";
  }

  return 0;
}
