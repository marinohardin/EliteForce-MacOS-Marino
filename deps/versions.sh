# Shared dependency pins for the macOS, Windows and Linux dependency builds.
# Sourced (not executed) by {macos,windows,linux}/build-{deps,ffmpeg}.sh so the
# platforms cannot drift. Bump version and checksum together and record
# the versions in THIRD_PARTY_LICENSES.md.

SDL3_VERSION="3.4.18"
SDL3_SHA256="9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3"
SDL2COMPAT_VERSION="2.32.74"
SDL2COMPAT_SHA256="ec68abde77e2e459c8abc4f9587b8976b5d2a657b2bfda2fb3a079b0f8924588"
# 5.8.4 (the 5.6.0/5.6.1 releases were the backdoored ones; stay well past them)
XZ_VERSION="5.8.4"
XZ_SHA256="4ce24038fd4221e0d13bc1a2de7a4db56e90b92b3bf75321f6c14be73f65de4b"
# Windows only (HTTP map downloads, sv_dlURL; loaded at run time as libcurl-4.dll)
CURL_VERSION="8.23.0"
CURL_SHA256="8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5"
FFMPEG_VERSION="9.0.2"
FFMPEG_SHA256="8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"
# Linux only: the Qt 6 Widgets launcher (ef-launcher), qtbase + qtwayland, LGPL-3.0,
# built and bundled as shared libraries by linux/build-qt.sh. 6.8.3 is the last
# open-source release of the 6.8 LTS line.
QT_VERSION="6.8.3"
QTBASE_SHA256="56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80"
QTWAYLAND_SHA256="20fe385887d21190165a3180c17dcfc8b9a0e1da4ec76865b6334bdc709994b0"
# macOS only (Windows ships the driver's vulkan-1.dll, Linux the system libvulkan.so.1)
VULKAN_VERSION="1.4.363.0"
VULKAN_SHA256="941eff558fc74f248745fb7df367640cf89a8457f216dae498a74ae14b6ba6f7"

# ef_sha256_check FILE SHA256: succeed when FILE matches (sha256sum or shasum)
ef_sha256_check() {
	if command -v sha256sum >/dev/null 2>&1; then
		echo "$2  $1" | sha256sum -c - >/dev/null 2>&1
	else
		echo "$2  $1" | shasum -a 256 -c - >/dev/null 2>&1
	fi
}
