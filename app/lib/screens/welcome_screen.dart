// =============================================================================
// MC Alarm — Welcome screen
// =============================================================================
//
// First screen the rider sees when the app launches and there is no BLE
// connection yet. Single big icon, the app name, and a START button that
// pushes the permissions screen.
//
// No state, no logic — pure presentation. The decision "show this or the
// dashboard?" is made in main.dart's _RootRouter, which swaps based on
// BleService.state.
// =============================================================================

import 'package:flutter/material.dart';

import '../theme/app_theme.dart';
import 'permissions_screen.dart';

class WelcomeScreen extends StatelessWidget {
  const WelcomeScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 28, vertical: 32),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Spacers around the content centre it vertically while keeping
              // the START button comfortably reachable at the bottom.
              const Spacer(),
              const Icon(Icons.warning_amber_rounded,
                  size: 120, color: AppTheme.primary),
              const SizedBox(height: 24),
              const Text('Welcome to the',
                  textAlign: TextAlign.center,
                  style: TextStyle(fontSize: 28, color: AppTheme.textSub)),
              const SizedBox(height: 8),
              const Text('MC Alarm',
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    fontSize: 52,
                    fontWeight: FontWeight.w800,
                    letterSpacing: 1.2,
                  )),
              const Spacer(),
              ElevatedButton(
                onPressed: () {
                  // Push (not pushReplacement) so the user can back out if
                  // they want. PermissionsScreen will pushReplacement to the
                  // ScanScreen once perms are granted.
                  Navigator.of(context).push(MaterialPageRoute(
                      builder: (_) => const PermissionsScreen()));
                },
                child: const Text('START'),
              ),
              const SizedBox(height: 24),
            ],
          ),
        ),
      ),
    );
  }
}
