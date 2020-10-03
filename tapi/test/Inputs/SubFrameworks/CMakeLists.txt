add_tapi_test_library(SubFrameworks
  FRAMEWORK
  SOURCE Framework.c
  ARCHITECTURES x86_64
  OUTPUT_DIRECTORY_PREFIX SubFrameworks
)

# Add a standard subframework
add_tapi_test_library(SubFrameworksStandard
  FRAMEWORK
  SOURCE SubFramework1.c
  ARCHITECTURES x86_64
  OUTPUT_DIRECTORY_PREFIX SubFrameworks
  OUTPUT_DIRECTORY_SUFFIX SubFrameworks.framework/Frameworks
)

# Add a non-standard subframework
add_tapi_test_library(SubFrameworksNonStandard
  FRAMEWORK
  SOURCE SubFramework2.c
  ARCHITECTURES x86_64
  OUTPUT_DIRECTORY_PREFIX SubFrameworks
  OUTPUT_DIRECTORY_SUFFIX SubFrameworks.framework/NonStandard
)
