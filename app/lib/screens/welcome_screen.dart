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
