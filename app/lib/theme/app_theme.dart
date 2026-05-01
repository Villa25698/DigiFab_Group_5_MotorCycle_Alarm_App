// =============================================================================
// MC Alarm — Theme
// =============================================================================
//
// One central place for the app's colours and "default button". Designed for
// a phone mounted on motorcycle handlebars:
//
//   - Dark background (less glare in sunlight, easier on night vision)
//   - High contrast yellow / red / green for status
//   - Big buttons (72px tall by default) so they're hittable with gloves
//   - Bold, oversized text — readable at a glance, not while studying
// =============================================================================

import 'package:flutter/material.dart';

class AppTheme {
  // ---- Colour palette ----
  // Use these named constants everywhere instead of raw hex codes so changing
  // a colour is a one-line edit.
  static const Color background = Color(0xFF0A0A0A);   // very dark grey
  static const Color surface    = Color(0xFF1A1A1A);   // cards / chips
  static const Color primary    = Color(0xFFFFB300);   // hazard amber
  static const Color danger     = Color(0xFFE53935);   // alarm red
  static const Color success    = Color(0xFF43A047);   // online green
  static const Color textOn     = Colors.white;        // body text
  static const Color textSub    = Color(0xFFB0B0B0);   // muted secondary text

  /// Build a Material 3 dark theme with our palette applied and the
  /// ElevatedButton default sized for handlebar use.
  static ThemeData build() {
    final base = ThemeData.dark(useMaterial3: true);
    return base.copyWith(
      scaffoldBackgroundColor: background,
      colorScheme: base.colorScheme.copyWith(
        primary:   primary,
        secondary: primary,     // we don't use a separate accent
        surface:   surface,
        error:     danger,
      ),
      textTheme: base.textTheme.apply(
        bodyColor:    textOn,
        displayColor: textOn,
      ),
      elevatedButtonTheme: ElevatedButtonThemeData(
        style: ElevatedButton.styleFrom(
          backgroundColor: primary,
          foregroundColor: Colors.black,
          textStyle: const TextStyle(
              fontSize: 22, fontWeight: FontWeight.w700),
          // Full-width by default. 72px is roughly Material's largest
          // recommended touch target — works well with gloves.
          minimumSize: const Size.fromHeight(72),
          shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(16)),
        ),
      ),
    );
  }
}
