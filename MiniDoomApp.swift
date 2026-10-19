import SwiftUI
import UIKit

@main
struct MiniDoomApp: App {
    @StateObject private var ble = ManetteBLE()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(ble)
        }
    }
}

struct ContentView: View {
    @EnvironmentObject var ble: ManetteBLE

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            VStack {
                // État de la connexion
                HStack(spacing: 8) {
                    Circle()
                        .fill(ble.connecte ? .green : .orange)
                        .frame(width: 10, height: 10)
                    Text(ble.etat)
                        .font(.subheadline)
                        .foregroundStyle(.white.opacity(0.7))
                }
                .padding(.top, 12)

                Spacer()

                // Joystick à gauche, bouton de tir à droite
                HStack {
                    Joystick { x, y in
                        ble.joyX = x
                        ble.joyY = y
                    }
                    Spacer()
                    BoutonTir { appuye in
                        ble.tir = appuye
                    }
                }
                .padding(.horizontal, 48)

                Spacer()
            }
        }
        .onAppear {
            UIApplication.shared.isIdleTimerDisabled = true   // l'écran ne se met pas en veille pendant le jeu
        }
    }
}

// MARK: - Joystick

struct Joystick: View {
    let onChange: (Int8, Int8) -> Void

    @State private var decalage: CGSize = .zero
    private let rayon: CGFloat = 70

    var body: some View {
        ZStack {
            Circle()
                .fill(.white.opacity(0.08))
            Circle()
                .stroke(.white.opacity(0.3), lineWidth: 2)
            Circle()
                .fill(.white.opacity(0.85))
                .frame(width: 60, height: 60)
                .offset(decalage)
        }
        .frame(width: rayon * 2, height: rayon * 2)
        .contentShape(Circle())
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { valeur in
                    var dx = valeur.translation.width
                    var dy = valeur.translation.height
                    let distance = sqrt(dx * dx + dy * dy)
                    if distance > rayon {          // on reste dans le cercle
                        dx = dx / distance * rayon
                        dy = dy / distance * rayon
                    }
                    decalage = CGSize(width: dx, height: dy)

                    // Droite = tourner à droite, haut = avancer
                    let x = Int8((dx / rayon * 100).rounded())
                    let y = Int8((-dy / rayon * 100).rounded())
                    onChange(x, y)
                }
                .onEnded { _ in
                    withAnimation(.spring(duration: 0.2)) { decalage = .zero }
                    onChange(0, 0)
                }
        )
    }
}

// MARK: - Bouton de tir

struct BoutonTir: View {
    let onChange: (Bool) -> Void

    @State private var appuye = false
    private let vibration = UIImpactFeedbackGenerator(style: .heavy)

    var body: some View {
        Circle()
            .fill(appuye ? Color.red : Color.red.opacity(0.6))
            .frame(width: 120, height: 120)
            .overlay(
                Text("TIR")
                    .font(.title2.bold())
                    .foregroundStyle(.white)
            )
            .scaleEffect(appuye ? 0.92 : 1)
            .animation(.easeOut(duration: 0.08), value: appuye)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if !appuye {
                            appuye = true
                            onChange(true)
                            vibration.impactOccurred()
                        }
                    }
                    .onEnded { _ in
                        appuye = false
                        onChange(false)
                    }
            )
    }
}
