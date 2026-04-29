import 'package:flutter/material.dart';

/// High-contrast dark theme with oversized touch targets.
/// The rider is moving — readability and finger-sized buttons beat
/// any kind of decorative flourish.
class AppTheme {
  static const Color background = Color(0xFF0A0A0A);
  static const Color surface    = Color(0xFF1A1A1A);
  static const Color primary    = Color(0xFFFFB300); // hazard amber
  static const Color danger     = Color(0xFFE53935);
  static const Color success    = Color(0xFF43A047);
  static const Color textOn     = Colors.white;
  static const Color textSub    = Color(0xFFB0B0B0);

  static ThemeData build() {
    final base = ThemeData.dark(useMaterial3: true);
    return base.copyWith(
      scaffoldBackgroundColor: background,
      colorScheme: base.colorScheme.copyWith(
        primary: primary,
        secondary: primary,
        surface: surface,
        error: danger,
      ),
      textTheme: base.textTheme.apply(
        bodyColor: textOn,
        displayColor: textOn,
      ),
      elevatedButtonTheme: ElevatedButtonThemeData(
        style: ElevatedButton.styleFrom(
          backgroundColor: primary,
          foregroundColor: Colors.black,
          textStyle: const TextStyle(fontSize: 22, fontWeight: FontWeight.w700),
          minimumSize: const Size.fromHeight(72),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
        ),
      ),
    );
  }
}
