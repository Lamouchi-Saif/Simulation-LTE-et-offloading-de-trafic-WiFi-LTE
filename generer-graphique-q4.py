
import csv
import os
import matplotlib.pyplot as plt

fichier_csv = "code/scratch/B/offloading-q4.csv"
fichier_image = "Rapport/latex/img/B/offloading-throughput.png"

temps = []
debits = []
interfaces = []

with open(fichier_csv, "r", newline="") as fichier:
    lecteur = csv.DictReader(fichier)

    for ligne in lecteur:
        temps.append(float(ligne["Time(s)"]))
        debits.append(float(ligne["Throughput(Mbps)"]))
        interfaces.append(ligne["Interface"])

plt.figure(figsize=(14, 6))

couleurs = {
    "LTE": "tab:blue",
    "WiFi": "tab:orange"
}


deja_affiche = set()
debut = 0

for i in range(1, len(temps) + 1):
    if i == len(temps) or interfaces[i] != interfaces[debut]:
        interface = interfaces[debut]

        label = interface if interface not in deja_affiche else "_nolegend_"

        plt.plot(
            temps[debut:i],
            debits[debut:i],
            color=couleurs[interface],
            label=label
        )

        deja_affiche.add(interface)
        debut = i


plt.axhline(
    y=0.4096,
    color="red",
    linestyle="--",
    label="Seuil 50 % (0.4096 Mbit/s)"
)

plt.axvline(
    x=14.5,
    color="green",
    linestyle=":",
    label="Bascule LTE vers Wi-Fi"
)

plt.axvline(
    x=26.5,
    color="purple",
    linestyle=":",
    label="Bascule Wi-Fi vers LTE"
)

plt.xlabel("Temps (s)")
plt.ylabel("Débit reçu (Mbit/s)")
plt.title("Évolution du débit pendant l'offloading LTE/Wi-Fi")
plt.grid(True)
plt.legend()
plt.tight_layout()

os.makedirs(os.path.dirname(fichier_image), exist_ok=True)
plt.savefig(fichier_image, dpi=300, bbox_inches="tight")
plt.show()

print("Graphique enregistré :", fichier_image)
