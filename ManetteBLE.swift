import Foundation
import Combine
import CoreBluetooth
import UIKit

/// Connexion BLE avec l'Arduino "Mini Doom" et envoi de l'état de la manette.
final class ManetteBLE: NSObject, ObservableObject {

    // UUID identiques à ceux du sketch Arduino
    static let serviceUUID = CBUUID(string: "D00D0000-1F2E-4A3B-8C7D-6E5F4A3B2C1D")
    static let manetteUUID = CBUUID(string: "D00D0001-1F2E-4A3B-8C7D-6E5F4A3B2C1D")
    static let retourUUID  = CBUUID(string: "D00D0002-1F2E-4A3B-8C7D-6E5F4A3B2C1D")

    @Published var connecte = false
    @Published var etat = "Démarrage…"

    // État de la manette, mis à jour par l'interface (de -100 à 100)
    var joyX: Int8 = 0
    var joyY: Int8 = 0
    var tir = false

    private var central: CBCentralManager!
    private var peripherique: CBPeripheral?
    private var carac: CBCharacteristic?
    private var timer: Timer?

    private var dernierEnvoi = Data()
    private var toursSansChangement = 0

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)

        // Envoi 20 fois par seconde
        timer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { [weak self] _ in
            self?.envoyer()
        }
    }

    private func envoyer() {
        guard connecte, let p = peripherique, let c = carac else { return }

        let data = Data([UInt8(bitPattern: joyX), UInt8(bitPattern: joyY), tir ? 1 : 0])

        // Si rien ne change, on renvoie quand même toutes les 0,5 s par sécurité
        if data == dernierEnvoi {
            toursSansChangement += 1
            if toursSansChangement < 10 { return }
        }

        if c.properties.contains(.writeWithoutResponse) {
            guard p.canSendWriteWithoutResponse else { return } // file d'envoi pleine : on réessaie au tour suivant
            p.writeValue(data, for: c, type: .withoutResponse)
        } else {
            p.writeValue(data, for: c, type: .withResponse)
        }
        dernierEnvoi = data
        toursSansChangement = 0
    }

    private func lancerRecherche() {
        guard central.state == .poweredOn else { return }
        etat = "Recherche de l'Arduino…"
        central.scanForPeripherals(withServices: [Self.serviceUUID])
    }
}

// MARK: - CBCentralManagerDelegate

extension ManetteBLE: CBCentralManagerDelegate {

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:    lancerRecherche()
        case .unauthorized: etat = "Autorise le Bluetooth dans Réglages"
        case .poweredOff:   etat = "Active le Bluetooth"
        default:            etat = "Bluetooth indisponible"
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        peripherique = peripheral
        central.stopScan()
        etat = "Connexion…"
        central.connect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.delegate = self
        peripheral.discoverServices([Self.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager,
                        didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        connecte = false
        carac = nil
        peripherique = nil
        lancerRecherche()
    }

    func centralManager(_ central: CBCentralManager,
                        didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        peripherique = nil
        lancerRecherche()
    }
}

// MARK: - CBPeripheralDelegate

extension ManetteBLE: CBPeripheralDelegate {

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        for service in peripheral.services ?? [] where service.uuid == Self.serviceUUID {
            peripheral.discoverCharacteristics([Self.manetteUUID, Self.retourUUID], for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        for c in service.characteristics ?? [] {
            if c.uuid == Self.manetteUUID {
                carac = c
                connecte = true
                etat = "Manette connectée"
            } else if c.uuid == Self.retourUUID {
                peripheral.setNotifyValue(true, for: c)   // reçoit les événements du jeu
            }
        }
    }

    // Événements envoyés par l'Arduino -> vibrations
    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard characteristic.uuid == Self.retourUUID,
              let code = characteristic.value?.first else { return }

        switch code {
        case 1:  // le joueur est touché
            UIImpactFeedbackGenerator(style: .heavy).impactOccurred(intensity: 1.0)
            etat = "Aïe, touché !"
        case 2:  // un monstre est touché
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
        case 3:  // un monstre est tué
            UIImpactFeedbackGenerator(style: .rigid).impactOccurred()
            etat = "Monstre éliminé !"
        case 4:  // game over
            UINotificationFeedbackGenerator().notificationOccurred(.error)
            etat = "Game over… appuie sur TIR"
        case 5:  // victoire
            UINotificationFeedbackGenerator().notificationOccurred(.success)
            etat = "Victoire ! Appuie sur TIR"
        default:
            break
        }
    }
}
