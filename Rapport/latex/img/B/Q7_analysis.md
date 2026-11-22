# Analyse Q7 — Distance vs RSSI

## 1. Validation expérimentale

- Nombre de graines appariées : 5.
- Nombre total de simulations : 10.
- Paramètres communs vérifiés.
- Absence de doublons.
- Vérification de nSwitch = nIn + nOut.
- Écart-type calculé avec ddof=1.

## 2. Statistiques descriptives

| criterion   |   n_runs |   n_seeds |   nSwitch_mean |   nSwitch_std |   nIn_mean |   nIn_std |   nOut_mean |   nOut_std |   nPingPong_mean |   nPingPong_std |   offloadTx_mean |   offloadTx_std |   offloadRx_mean |   offloadRx_std |   tputMean_Mbps_mean |   tputMean_Mbps_std |   tputStd_Mbps_mean |   tputStd_Mbps_std |   tputCV_mean |   tputCV_std |   lossPct_mean |   lossPct_std |   delayMean_ms_mean |   delayMean_ms_std |   delayMax_ms_mean |   delayMax_ms_std |   outage_s_mean |   outage_s_std |   tcpTput_Mbps_mean |   tcpTput_Mbps_std |
|:------------|---------:|----------:|---------------:|--------------:|-----------:|----------:|------------:|-----------:|-----------------:|----------------:|-----------------:|----------------:|-----------------:|----------------:|---------------------:|--------------------:|--------------------:|-------------------:|--------------:|-------------:|---------------:|--------------:|--------------------:|-------------------:|-------------------:|------------------:|----------------:|---------------:|--------------------:|-------------------:|
| distance    |        5 |         5 |        20.0000 |        0.0000 |    10.0000 |    0.0000 |     10.0000 |     0.0000 |          19.0000 |          0.0000 |           0.4884 |          0.0281 |           0.4884 |          0.0281 |               2.0000 |              0.0000 |              0.0168 |             0.0001 |        0.0084 |       0.0001 |         0.0000 |        0.0000 |              9.5819 |             0.4579 |            22.7307 |            0.0000 |          0.0000 |         0.0000 |              0.0000 |             0.0000 |
| rssi        |        5 |         5 |        19.4000 |        0.5477 |    10.0000 |    0.0000 |      9.4000 |     0.5477 |          18.4000 |          0.5477 |           0.4525 |          0.0299 |           0.4525 |          0.0299 |               2.0000 |              0.0000 |              0.0168 |             0.0001 |        0.0084 |       0.0001 |         0.0000 |        0.0000 |             10.1667 |             0.4871 |            22.7307 |            0.0000 |          0.0000 |         0.0000 |              0.0000 |             0.0000 |

## 3. Différences appariées

Convention : différence = RSSI - Distance.

|   seed |   distance_nSwitch |   rssi_nSwitch |   delta_nSwitch |   distance_nIn |   rssi_nIn |   delta_nIn |   distance_nOut |   rssi_nOut |   delta_nOut |   distance_nPingPong |   rssi_nPingPong |   delta_nPingPong |   distance_offloadTx |   rssi_offloadTx |   delta_offloadTx |   distance_offloadRx |   rssi_offloadRx |   delta_offloadRx |   distance_tputMean_Mbps |   rssi_tputMean_Mbps |   delta_tputMean_Mbps |   distance_tputStd_Mbps |   rssi_tputStd_Mbps |   delta_tputStd_Mbps |   distance_tputCV |   rssi_tputCV |   delta_tputCV |   distance_lossPct |   rssi_lossPct |   delta_lossPct |   distance_delayMean_ms |   rssi_delayMean_ms |   delta_delayMean_ms |   distance_delayMax_ms |   rssi_delayMax_ms |   delta_delayMax_ms |   distance_outage_s |   rssi_outage_s |   delta_outage_s |   distance_tcpTput_Mbps |   rssi_tcpTput_Mbps |   delta_tcpTput_Mbps |
|-------:|-------------------:|---------------:|----------------:|---------------:|-----------:|------------:|----------------:|------------:|-------------:|---------------------:|-----------------:|------------------:|---------------------:|-----------------:|------------------:|---------------------:|-----------------:|------------------:|-------------------------:|---------------------:|----------------------:|------------------------:|--------------------:|---------------------:|------------------:|--------------:|---------------:|-------------------:|---------------:|----------------:|------------------------:|--------------------:|---------------------:|-----------------------:|-------------------:|--------------------:|--------------------:|----------------:|-----------------:|------------------------:|--------------------:|---------------------:|
| 1.0000 |            20.0000 |        20.0000 |          0.0000 |        10.0000 |    10.0000 |      0.0000 |         10.0000 |     10.0000 |       0.0000 |              19.0000 |          19.0000 |            0.0000 |               0.4423 |           0.4102 |           -0.0321 |               0.4423 |           0.4102 |           -0.0321 |                   2.0000 |               2.0000 |                0.0000 |                  0.0169 |              0.0169 |               0.0000 |            0.0085 |        0.0085 |         0.0000 |             0.0000 |         0.0000 |          0.0000 |                 10.3355 |             10.8576 |               0.5221 |                22.7307 |            22.7307 |              0.0000 |              0.0000 |          0.0000 |           0.0000 |                  0.0000 |              0.0000 |               0.0000 |
| 2.0000 |            20.0000 |        20.0000 |          0.0000 |        10.0000 |    10.0000 |      0.0000 |         10.0000 |     10.0000 |       0.0000 |              19.0000 |          19.0000 |            0.0000 |               0.4807 |           0.4358 |           -0.0449 |               0.4807 |           0.4358 |           -0.0449 |                   2.0000 |               2.0000 |                0.0000 |                  0.0167 |              0.0169 |               0.0002 |            0.0084 |        0.0085 |         0.0001 |             0.0000 |         0.0000 |          0.0000 |                  9.7042 |             10.4352 |               0.7310 |                22.7307 |            22.7307 |              0.0000 |              0.0000 |          0.0000 |           0.0000 |                  0.0000 |              0.0000 |               0.0000 |
| 3.0000 |            20.0000 |        19.0000 |         -1.0000 |        10.0000 |    10.0000 |      0.0000 |         10.0000 |      9.0000 |      -1.0000 |              19.0000 |          18.0000 |           -1.0000 |               0.5064 |           0.4615 |           -0.0449 |               0.5064 |           0.4615 |           -0.0449 |                   2.0000 |               2.0000 |                0.0000 |                  0.0169 |              0.0167 |              -0.0002 |            0.0085 |        0.0084 |        -0.0001 |             0.0000 |         0.0000 |          0.0000 |                  9.2918 |             10.0221 |               0.7303 |                22.7307 |            22.7307 |              0.0000 |              0.0000 |          0.0000 |           0.0000 |                  0.0000 |              0.0000 |               0.0000 |
| 4.0000 |            20.0000 |        19.0000 |         -1.0000 |        10.0000 |    10.0000 |      0.0000 |         10.0000 |      9.0000 |      -1.0000 |              19.0000 |          18.0000 |           -1.0000 |               0.5064 |           0.4679 |           -0.0385 |               0.5064 |           0.4679 |           -0.0385 |                   2.0000 |               2.0000 |                0.0000 |                  0.0167 |              0.0167 |               0.0000 |            0.0084 |        0.0084 |         0.0000 |             0.0000 |         0.0000 |          0.0000 |                  9.2856 |              9.9121 |               0.6265 |                22.7307 |            22.7307 |              0.0000 |              0.0000 |          0.0000 |           0.0000 |                  0.0000 |              0.0000 |               0.0000 |
| 5.0000 |            20.0000 |        19.0000 |         -1.0000 |        10.0000 |    10.0000 |      0.0000 |         10.0000 |      9.0000 |      -1.0000 |              19.0000 |          18.0000 |           -1.0000 |               0.5064 |           0.4871 |           -0.0193 |               0.5064 |           0.4871 |           -0.0193 |                   2.0000 |               2.0000 |                0.0000 |                  0.0169 |              0.0169 |               0.0000 |            0.0085 |        0.0085 |         0.0000 |             0.0000 |         0.0000 |          0.0000 |                  9.2924 |              9.6063 |               0.3139 |                22.7307 |            22.7307 |              0.0000 |              0.0000 |          0.0000 |           0.0000 |                  0.0000 |              0.0000 |               0.0000 |

### Synthèse des bascules

- Différence moyenne : -0.6000.
- Écart-type des différences : 0.5477.
- RSSI avec moins de bascules : 3/5.
- Même nombre de bascules : 2/5.
- RSSI avec plus de bascules : 0/5.

## 4. Tests statistiques

| metric        |   n_pairs |   mean_delta |   std_delta |   wilcoxon_statistic |   p_value | note               |
|:--------------|----------:|-------------:|------------:|---------------------:|----------:|:-------------------|
| nSwitch       |         5 |      -0.6000 |      0.5477 |                  nan |       nan | SciPy non installé |
| offloadTx     |         5 |      -0.0359 |      0.0107 |                  nan |       nan | SciPy non installé |
| delayMean_ms  |         5 |       0.5847 |      0.1744 |                  nan |       nan | SciPy non installé |
| tputMean_Mbps |         5 |       0.0000 |      0.0000 |                  nan |       nan | SciPy non installé |

## 5. Interprétation

Dans un scénario sans obstacles et sans shadowing, le RSSI est fortement lié à la distance géométrique. Les différences éventuelles entre les critères peuvent provenir de la réception des trames, du lissage EWMA, du délai d'expiration RSSI et de la période de contrôle.

Dans un environnement urbain dense ou indoor, les obstacles, l'atténuation et les trajets multiples peuvent rendre la distance géométrique moins représentative de la qualité réelle du lien radio. Le RSSI peut alors fournir une indication plus pertinente de l'état du canal, sous réserve de filtrer ses variations rapides.

Les résultats doivent être interprétés dans les limites du scénario simulé et du nombre de graines étudiées.
