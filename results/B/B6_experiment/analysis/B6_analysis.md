# Analyse de la campagne B6

## Protocole observé dans les CSV
- Nombre de simulations : 45
- Rayons : 40 m, 50 m, 60 m
- Vitesses : 3 m/s, 5 m/s, 7 m/s
- Graines : 1, 2, 3, 4, 5
- Critère : distance
- Trajectoire : line

## Tableau des résultats

| Rayon (m) | Vitesse (m/s) | n | Offloading moyen (%) | Écart-type (points de %) |
|---:|---:|---:|---:|---:|
| 40 | 3 | 5 | 33.7132 | 0.3505 |
| 40 | 5 | 5 | 19.8708 | 0.0000 |
| 40 | 7 | 5 | 14.1046 | 0.0000 |
| 50 | 3 | 5 | 42.0505 | 0.5752 |
| 50 | 5 | 5 | 24.9969 | 0.0000 |
| 50 | 7 | 5 | 17.9508 | 0.0000 |
| 60 | 3 | 5 | 50.6437 | 0.6431 |
| 60 | 5 | 5 | 30.1292 | 0.0000 |
| 60 | 7 | 5 | 21.7969 | 0.0000 |

## Contrôles de validation

Aucune anomalie bloquante détectée par les contrôles automatisés.

## Interprétation à conduire

- Comparer les moyennes entre rayons à vitesse constante.
- Comparer les moyennes entre vitesses à rayon constant.
- Comparer les écarts-types inter-graines à l'amplitude des variations de moyenne.
- Ne pas interpréter un écart-type nul comme une absence générale d'aléa : il signifie seulement que les cinq résultats mesurés sont identiques à la précision des compteurs.

## Limites du protocole

Les CSV indiquent shadowSigma=0 et distNoise=0. La variabilité entre graines provient donc principalement du décalage latéral aléatoire offset_b. Elle ne représente pas une variabilité de shadowing radio. Les résultats doivent être discutés dans ce cadre.

Le champ offloadTx est recalculé à partir des compteurs txBytesWifi et txBytesLte. La formule utilisée est : txBytesWifi / (txBytesWifi + txBytesLte).
