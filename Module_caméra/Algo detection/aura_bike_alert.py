import cv2
from ultralytics import YOLO

# Chargement du modèle
model = YOLO("yolov8n.pt")

# Classes
DANGER_CLASSES = {"car", "motorcycle", "bus", "truck"}
INFO_CLASSES = {"bicycle"}

# Ouvrir la vidéo
cap = cv2.VideoCapture("video.mp4")

if not cap.isOpened():
    print("Erreur : impossible d'ouvrir la vidéo.")
    exit()

# Récupération des propriétés de la vidéo
fps = cap.get(cv2.CAP_PROP_FPS)
if fps == 0:
    fps = 30  # valeur par défaut si le FPS n'est pas lisible

width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

# Préparer l'enregistrement de la vidéo annotée
fourcc = cv2.VideoWriter_fourcc(*"mp4v")
out = cv2.VideoWriter("aura_bike_detection_output.mp4", fourcc, fps, (width, height))

while True:
    ret, frame = cap.read()
    if not ret:
        break

    h, w = frame.shape[:2]

    # Zones horizontales
    left_limit = int(w * 0.33)
    right_limit = int(w * 0.66)

    results = model(frame, verbose=False)
    annotated = frame.copy()

    # Etat global
    global_alert_level = "SAFE"
    global_alert_color = (60, 180, 75)   # vert doux
    global_alert_text = "Route degagee"
    global_position = "-"

    cyclist_info_text = ""
    cyclist_info_color = (255, 200, 0)

    if len(results) > 0:
        result = results[0]
        boxes = result.boxes
        names = result.names

        if boxes is not None:
            for box, cls_id, conf in zip(
                boxes.xyxy.cpu().numpy(),
                boxes.cls.cpu().numpy(),
                boxes.conf.cpu().numpy()
            ):
                class_name = names[int(cls_id)]

                if class_name not in DANGER_CLASSES and class_name not in INFO_CLASSES:
                    continue

                x1, y1, x2, y2 = map(int, box)
                bw = max(1, x2 - x1)
                bh = max(1, y2 - y1)
                area = bw * bh
                cx = (x1 + x2) // 2

                # Position
                if cx < left_limit:
                    position = "GAUCHE"
                elif cx > right_limit:
                    position = "DROITE"
                else:
                    position = "CENTRE"

                # -----------------------------
                # CAS 1 : VEHICULES DANGEREUX
                # -----------------------------
                if class_name in DANGER_CLASSES:
                    danger_score = 0

                    if area > 4000:
                        danger_score += 1
                    if area > 12000:
                        danger_score += 1
                    if position == "CENTRE":
                        danger_score += 1

                    if danger_score <= 1:
                        level = "LOW"
                        color = (0, 220, 0)
                        alert_text = "Vehicule detecte"
                    elif danger_score == 2:
                        level = "MEDIUM"
                        color = (0, 165, 255)
                        alert_text = "Attention arriere"
                    else:
                        level = "HIGH"
                        color = (0, 0, 255)
                        alert_text = "Danger arriere"

                    cv2.rectangle(annotated, (x1, y1), (x2, y2), color, 2)
                    label = f"{class_name} | {position} | {level}"
                    cv2.putText(
                        annotated,
                        label,
                        (x1, max(25, y1 - 8)),
                        cv2.FONT_HERSHEY_SIMPLEX,
                        0.5,
                        color,
                        2
                    )

                    priority = {"SAFE": 0, "LOW": 1, "MEDIUM": 2, "HIGH": 3}
                    if priority[level] > priority[global_alert_level]:
                        global_alert_level = level
                        global_alert_color = color
                        global_alert_text = alert_text
                        global_position = position

                # -----------------------------
                # CAS 2 : CYCLISTE = INFO
                # -----------------------------
                elif class_name in INFO_CLASSES:
                    # On n'affiche l'info que s'il est assez proche
                    if area > 5000:
                        color = (255, 200, 0)
                        cv2.rectangle(annotated, (x1, y1), (x2, y2), color, 2)

                        label = f"cycliste proche | {position}"
                        cv2.putText(
                            annotated,
                            label,
                            (x1, max(25, y1 - 8)),
                            cv2.FONT_HERSHEY_SIMPLEX,
                            0.5,
                            color,
                            2
                        )

                        cyclist_info_text = f"Cycliste proche - {position}"
                        cyclist_info_color = color

    # Lignes de séparation discrètes
    cv2.line(annotated, (left_limit, 0), (left_limit, h), (180, 180, 180), 1)
    cv2.line(annotated, (right_limit, 0), (right_limit, h), (180, 180, 180), 1)

    # Bandeau haut plus discret
    bar_height = 42
    cv2.rectangle(annotated, (0, 0), (w, bar_height), (30, 30, 30), -1)

    # Pastille couleur à gauche
    cv2.circle(annotated, (20, 21), 8, global_alert_color, -1)

    cv2.putText(
        annotated,
        f"Aura Bike | {global_alert_text}",
        (40, 27),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.58,
        (255, 255, 255),
        2
    )

    # Niveau et position à droite
    right_text = f"{global_alert_level} | {global_position}"
    text_size = cv2.getTextSize(right_text, cv2.FONT_HERSHEY_SIMPLEX, 0.52, 2)[0]
    cv2.putText(
        annotated,
        right_text,
        (w - text_size[0] - 15, 27),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.52,
        (220, 220, 220),
        2
    )

    # Info cycliste en bas si besoin
    if cyclist_info_text:
        cv2.rectangle(annotated, (10, h - 40), (270, h - 10), (40, 40, 40), -1)
        cv2.putText(
            annotated,
            cyclist_info_text,
            (20, h - 18),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            cyclist_info_color,
            2
        )

    # Affichage en direct
    cv2.imshow("Aura Bike - Detection & Alert", annotated)

    # Enregistrement de la frame annotée
    out.write(annotated)

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
out.release()
cv2.destroyAllWindows()

print("Vidéo enregistrée : aura_bike_detection_output.mp4")