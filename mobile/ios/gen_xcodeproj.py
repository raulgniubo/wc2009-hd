#!/usr/bin/env python3
"""Writes WC2009HD.xcodeproj/project.pbxproj (run from anywhere; no Xcode needed)."""
import hashlib
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def uid(name):
    return hashlib.md5(name.encode()).hexdigest()[:24].upper()


files = {
    # name: (path relative to the group, file type, phase)
    "main.mm": ("main.mm", "sourcecode.cpp.objcpp", "sources"),
    "Info.plist": ("Info.plist", "text.plist.xml", None),
    "Assets.xcassets": ("Assets.xcassets", "folder.assetcatalog", "resources"),
    "Inter-Medium.ttf": ("fonts/Inter-Medium.ttf", "file", "resources"),
    "Inter-Bold.ttf": ("fonts/Inter-Bold.ttf", "file", "resources"),
}
frameworks = ["OpenGLES", "GLKit", "AVFoundation", "GameController", "UIKit", "QuartzCore", "CoreGraphics", "AudioToolbox"]

P = uid("project"); T = uid("target"); MAIN = uid("maingroup"); APPG = uid("appgroup"); FWG = uid("fwgroup")
PRODG = uid("prodgroup"); PROD = uid("product")
SRC = uid("phase.sources"); RES = uid("phase.resources"); FW = uid("phase.frameworks"); SCRIPT = uid("phase.script")
PCL = uid("cfglist.project"); TCL = uid("cfglist.target")
PD, PR, TD, TR = uid("cfg.p.debug"), uid("cfg.p.release"), uid("cfg.t.debug"), uid("cfg.t.release")

out = []
w = out.append
w("// !$*UTF8*$!\n{\n\tarchiveVersion = 1;\n\tclasses = {\n\t};\n\tobjectVersion = 56;\n\tobjects = {\n")

w("\n/* Begin PBXBuildFile section */\n")
for n, (p, t, ph) in files.items():
    if ph:
        w(f"\t\t{uid('bf.' + n)} /* {n} */ = {{isa = PBXBuildFile; fileRef = {uid('fr.' + n)} /* {n} */; }};\n")
for f in frameworks:
    w(f"\t\t{uid('bf.fw.' + f)} /* {f}.framework */ = {{isa = PBXBuildFile; fileRef = {uid('fr.fw.' + f)} /* {f}.framework */; }};\n")
w("/* End PBXBuildFile section */\n")

w("\n/* Begin PBXFileReference section */\n")
for n, (p, t, ph) in files.items():
    w(f"\t\t{uid('fr.' + n)} /* {n} */ = {{isa = PBXFileReference; lastKnownFileType = {t}; path = \"{p}\"; sourceTree = \"<group>\"; }};\n")
for f in frameworks:
    w(f"\t\t{uid('fr.fw.' + f)} /* {f}.framework */ = {{isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = {f}.framework; path = System/Library/Frameworks/{f}.framework; sourceTree = SDKROOT; }};\n")
w(f"\t\t{PROD} /* WC2009HD.app */ = {{isa = PBXFileReference; explicitFileType = wrapper.application; includeInIndex = 0; path = WC2009HD.app; sourceTree = BUILT_PRODUCTS_DIR; }};\n")
w("/* End PBXFileReference section */\n")

w("\n/* Begin PBXFrameworksBuildPhase section */\n")
w(f"\t\t{FW} /* Frameworks */ = {{\n\t\t\tisa = PBXFrameworksBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n")
for f in frameworks:
    w(f"\t\t\t\t{uid('bf.fw.' + f)} /* {f}.framework */,\n")
w("\t\t\t);\n\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n/* End PBXFrameworksBuildPhase section */\n")

w("\n/* Begin PBXGroup section */\n")
w(f"\t\t{MAIN} = {{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n\t\t\t\t{APPG} /* WC2009HD */,\n\t\t\t\t{FWG} /* Frameworks */,\n\t\t\t\t{PRODG} /* Products */,\n\t\t\t);\n\t\t\tsourceTree = \"<group>\";\n\t\t}};\n")
w(f"\t\t{APPG} /* WC2009HD */ = {{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n")
for n in files:
    w(f"\t\t\t\t{uid('fr.' + n)} /* {n} */,\n")
w("\t\t\t);\n\t\t\tpath = WC2009HD;\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n")
w(f"\t\t{FWG} /* Frameworks */ = {{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n")
for f in frameworks:
    w(f"\t\t\t\t{uid('fr.fw.' + f)} /* {f}.framework */,\n")
w("\t\t\t);\n\t\t\tname = Frameworks;\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n")
w(f"\t\t{PRODG} /* Products */ = {{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n\t\t\t\t{PROD} /* WC2009HD.app */,\n\t\t\t);\n\t\t\tname = Products;\n\t\t\tsourceTree = \"<group>\";\n\t\t}};\n")
w("/* End PBXGroup section */\n")

w("\n/* Begin PBXNativeTarget section */\n")
w(f"""\t\t{T} /* WC2009HD */ = {{
\t\t\tisa = PBXNativeTarget;
\t\t\tbuildConfigurationList = {TCL};
\t\t\tbuildPhases = (
\t\t\t\t{SCRIPT} /* Core library */,
\t\t\t\t{SRC} /* Sources */,
\t\t\t\t{FW} /* Frameworks */,
\t\t\t\t{RES} /* Resources */,
\t\t\t);
\t\t\tbuildRules = (
\t\t\t);
\t\t\tdependencies = (
\t\t\t);
\t\t\tname = WC2009HD;
\t\t\tproductName = WC2009HD;
\t\t\tproductReference = {PROD} /* WC2009HD.app */;
\t\t\tproductType = "com.apple.product-type.application";
\t\t}};
""")
w("/* End PBXNativeTarget section */\n")

w("\n/* Begin PBXProject section */\n")
w(f"""\t\t{P} /* Project object */ = {{
\t\t\tisa = PBXProject;
\t\t\tattributes = {{
\t\t\t\tBuildIndependentTargetsInParallel = 1;
\t\t\t\tLastUpgradeCheck = 1600;
\t\t\t\tTargetAttributes = {{
\t\t\t\t\t{T} = {{
\t\t\t\t\t\tCreatedOnToolsVersion = 15.0;
\t\t\t\t\t}};
\t\t\t\t}};
\t\t\t}};
\t\t\tbuildConfigurationList = {PCL};
\t\t\tcompatibilityVersion = "Xcode 14.0";
\t\t\tdevelopmentRegion = en;
\t\t\thasScannedForEncodings = 0;
\t\t\tknownRegions = (
\t\t\t\ten,
\t\t\t\tBase,
\t\t\t);
\t\t\tmainGroup = {MAIN};
\t\t\tproductRefGroup = {PRODG} /* Products */;
\t\t\tprojectDirPath = "";
\t\t\tprojectRoot = "";
\t\t\ttargets = (
\t\t\t\t{T} /* WC2009HD */,
\t\t\t);
\t\t}};
""")
w("/* End PBXProject section */\n")

w("\n/* Begin PBXResourcesBuildPhase section */\n")
w(f"\t\t{RES} /* Resources */ = {{\n\t\t\tisa = PBXResourcesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n")
for n, (p, t, ph) in files.items():
    if ph == "resources":
        w(f"\t\t\t\t{uid('bf.' + n)} /* {n} */,\n")
w("\t\t\t);\n\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n/* End PBXResourcesBuildPhase section */\n")

script = ('if [ ! -f \\"$SRCROOT/lib/libwc09.a\\" ]; then\\n'
          '  export PATH=\\"/opt/homebrew/bin:/usr/local/bin:$PATH\\"\\n'
          '  if command -v cmake >/dev/null; then\\n'
          '    \\"$SRCROOT/build_ios_lib.sh\\"\\n'
          '  else\\n'
          '    echo \\"error: lib/libwc09.a is missing. Use the ready-made iOS download, or install cmake (brew install cmake) so Xcode can build it.\\"\\n'
          '    exit 1\\n'
          '  fi\\n'
          'fi\\n')
w("\n/* Begin PBXShellScriptBuildPhase section */\n")
w(f"""\t\t{SCRIPT} /* Core library */ = {{
\t\t\tisa = PBXShellScriptBuildPhase;
\t\t\talwaysOutOfDate = 1;
\t\t\tbuildActionMask = 2147483647;
\t\t\tfiles = (
\t\t\t);
\t\t\tinputPaths = (
\t\t\t);
\t\t\tname = "Core library";
\t\t\toutputPaths = (
\t\t\t);
\t\t\trunOnlyForDeploymentPostprocessing = 0;
\t\t\tshellPath = /bin/sh;
\t\t\tshellScript = "{script}";
\t\t}};
""")
w("/* End PBXShellScriptBuildPhase section */\n")

w("\n/* Begin PBXSourcesBuildPhase section */\n")
w(f"\t\t{SRC} /* Sources */ = {{\n\t\t\tisa = PBXSourcesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n")
for n, (p, t, ph) in files.items():
    if ph == "sources":
        w(f"\t\t\t\t{uid('bf.' + n)} /* {n} */,\n")
w("\t\t\t);\n\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n/* End PBXSourcesBuildPhase section */\n")

common_project = {
    "ALWAYS_SEARCH_USER_PATHS": "NO",
    "CLANG_CXX_LANGUAGE_STANDARD": '"c++17"',
    "CLANG_ENABLE_MODULES": "YES",
    "CLANG_ENABLE_OBJC_ARC": "YES",
    "ENABLE_USER_SCRIPT_SANDBOXING": "NO",
    "IPHONEOS_DEPLOYMENT_TARGET": "15.0",
    "SDKROOT": "iphoneos",
    "GCC_C_LANGUAGE_STANDARD": "gnu11",
}
target_common = {
    "ASSETCATALOG_COMPILER_APPICON_NAME": "AppIcon",
    "CODE_SIGN_STYLE": "Automatic",
    "CURRENT_PROJECT_VERSION": "1",
    "DEVELOPMENT_TEAM": '""',
    "ENABLE_BITCODE": "NO",
    "GCC_PREPROCESSOR_DEFINITIONS": '("GLES_SILENCE_DEPRECATION=1", "$(inherited)")',
    "HEADER_SEARCH_PATHS": '("$(SRCROOT)/include", "$(SRCROOT)/../core")',
    "INFOPLIST_FILE": "WC2009HD/Info.plist",
    "LD_RUNPATH_SEARCH_PATHS": '("$(inherited)", "@executable_path/Frameworks")',
    "LIBRARY_SEARCH_PATHS": '("$(SRCROOT)/lib")',
    "MARKETING_VERSION": "1.0",
    "OTHER_LDFLAGS": '("-lwc09", "-lc++")',
    "PRODUCT_BUNDLE_IDENTIFIER": "io.github.wc2009hd.app",
    "PRODUCT_NAME": '"$(TARGET_NAME)"',
    "SUPPORTED_PLATFORMS": '"iphoneos"',
    "SUPPORTS_MACCATALYST": "NO",
    "TARGETED_DEVICE_FAMILY": '"1,2"',
    "ARCHS": "arm64",
}


def cfg(id_, name, settings):
    s = "".join(f"\t\t\t\t{k} = {v};\n" for k, v in sorted(settings.items()))
    return f"\t\t{id_} /* {name} */ = {{\n\t\t\tisa = XCBuildConfiguration;\n\t\t\tbuildSettings = {{\n{s}\t\t\t}};\n\t\t\tname = {name};\n\t\t}};\n"


w("\n/* Begin XCBuildConfiguration section */\n")
w(cfg(PD, "Debug", {**common_project, "ONLY_ACTIVE_ARCH": "YES", "GCC_OPTIMIZATION_LEVEL": "0", "DEBUG_INFORMATION_FORMAT": "dwarf"}))
w(cfg(PR, "Release", {**common_project, "GCC_OPTIMIZATION_LEVEL": "s", "VALIDATE_PRODUCT": "YES"}))
w(cfg(TD, "Debug", target_common))
w(cfg(TR, "Release", target_common))
w("/* End XCBuildConfiguration section */\n")

w("\n/* Begin XCConfigurationList section */\n")
for id_, a, b in ((PCL, PD, PR), (TCL, TD, TR)):
    w(f"\t\t{id_} = {{\n\t\t\tisa = XCConfigurationList;\n\t\t\tbuildConfigurations = (\n\t\t\t\t{a} /* Debug */,\n\t\t\t\t{b} /* Release */,\n\t\t\t);\n\t\t\tdefaultConfigurationIsVisible = 0;\n\t\t\tdefaultConfigurationName = Release;\n\t\t}};\n")
w("/* End XCConfigurationList section */\n")

w(f"\t}};\n\trootObject = {P} /* Project object */;\n}}\n")

proj = os.path.join(HERE, "WC2009HD.xcodeproj")
os.makedirs(proj, exist_ok=True)
with open(os.path.join(proj, "project.pbxproj"), "w") as f:
    f.write("".join(out))
print("wrote", os.path.join(proj, "project.pbxproj"))
