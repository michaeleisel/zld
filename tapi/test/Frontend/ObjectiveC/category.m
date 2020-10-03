// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -isysroot %sysroot %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi -isysroot %sysroot %s 2>&1 | FileCheck %s

@class NSSet;

// CHECK-LABEL: objective-c interfaces:
// CHECK:      - name: Foo
// CHECK:        categories: Bar CoreDataGeneratedAccessors
@interface Foo {
@public
  int ivar1;
}
@property int prop1;
- (void)method1;
@end

// CHECK-LABEL: objective-c categories:
// CHECK-NEXT:  - name: Bar
// CHECK-NEXT:    interfaceName: Foo
// CHECK-NEXT:    loc:
// CHECK-NEXT:    availability: i:0 o:0 u:0
// CHECK-NEXT:    protocols:
// CHECK-NEXT:    methods:
// CHECK-NEXT:    - name: method2
// CHECK-NEXT:      kind: instance
// CHECK-NEXT:      isOptional: false
// CHECK-NEXT:      isDynamic: false
// CHECK-NEXT:      loc:
// CHECK-NEXT:      availability: i:0 o:0 u:0
// CHECK-NEXT:    properties:
// CHECK-NEXT:    - name: prop2
// CHECK-NEXT:      attributes:
// CHECK-NEXT:      isOptional: false
// CHECK-NEXT:      getter name: prop2
// CHECK-NEXT:      setter name: setProp2:
// CHECK-NEXT:      loc:
// CHECK-NEXT:      availability: i:0 o:0 u:0
// CHECK-NEXT:    instance variables:
@interface Foo (Bar)
@property int prop2;
- (void)method2;
@end

// CoreData Accessors are dynamically generated and have no implementation.
// CHECK-NEXT:  - name: CoreDataGeneratedAccessors
// CHECK-NEXT:    interfaceName: Foo
// CHECK-NEXT:    loc:
// CHECK-NEXT:    availability: i:0 o:0 u:0
// CHECK-NEXT:    protocols:
// CHECK-NEXT:    methods:
// CHECK-NEXT:    - name: addChildObject:
// CHECK-NEXT:      kind: instance
// CHECK-NEXT:      isOptional: false
// CHECK-NEXT:      isDynamic: true
// CHECK-NEXT:      loc:
// CHECK-NEXT:      availability: i:0 o:0 u:0
// CHECK-NEXT:    - name: removeChildObject:
// CHECK-NEXT:      kind: instance
// CHECK-NEXT:      isOptional: false
// CHECK-NEXT:      isDynamic: true
// CHECK-NEXT:      loc:
// CHECK-NEXT:      availability: i:0 o:0 u:0
// CHECK-NEXT:    - name: addChild:
// CHECK-NEXT:      kind: instance
// CHECK-NEXT:      isOptional: false
// CHECK-NEXT:      isDynamic: true
// CHECK-NEXT:      loc:
// CHECK-NEXT:      availability: i:0 o:0 u:0
// CHECK-NEXT:    - name: removeChild:
// CHECK-NEXT:      kind: instance
// CHECK-NEXT:      isOptional: false
// CHECK-NEXT:      isDynamic: true
// CHECK-NEXT:      loc:
// CHECK-NEXT:      availability: i:0 o:0 u:0
// CHECK-NEXT:    properties:
// CHECK-NEXT:    instance variables:
@interface Foo (CoreDataGeneratedAccessors)
- (void)addChildObject:(Foo *)value;
- (void)removeChildObject:(Foo *)value;
- (void)addChild:(NSSet *)values;
- (void)removeChild:(NSSet *)values;
@end

// CHECK-NEXT: - name:
// CHECK-NEXT:   interfaceName: Foo
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:0 o:0 u:0
// CHECK-NEXT:   protocols:
// CHECK-NEXT:   methods:
// CHECK-NEXT:   - name: method3
// CHECK-NEXT:     kind: instance
// CHECK-NEXT:     isOptional: false
// CHECK-NEXT:     isDynamic: false
// CHECK-NEXT:     loc:
// CHECK-NEXT:     availability: i:0 o:0 u:0
// CHECK-NEXT:   properties:
// CHECK-NEXT:   - name: prop3
// CHECK-NEXT:     attributes:
// CHECK-NEXT:     isOptional: false
// CHECK-NEXT:     getter name: prop3
// CHECK-NEXT:     setter name: setProp3:
// CHECK-NEXT:     loc:
// CHECK-NEXT:     availability: i:0 o:0 u:0
// CHECK-NEXT:   instance variables:
// CHECK-NEXT:   - name: ivar3
// CHECK-NEXT:     loc:
// CHECK-NEXT:     access: public
@interface Foo () {
@public
  int ivar3;
}
@property int prop3;
- (void)method3;
@end
