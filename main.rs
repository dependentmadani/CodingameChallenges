use std::collections::HashMap;
use std::io;

macro_rules! parse_input {
    ($x:expr, $t:ident) => {
        $x.trim().parse::<$t>().unwrap()
    };
}

fn search_for_root_basic_or_a_in_owner(
    store_positions_type: &HashMap<(i32, i32), String>,
    store_owner_organ: &HashMap<(i32, i32), i32>
) -> Vec<(i32, i32)> {
    let mut root_basic_or_a_positions = Vec::new();
    
    // Iterate over the HashMap and find positions where the value is ROOT, BASIC, or "A"
    for (&(x, y), value) in store_positions_type.iter() {
        if (value == "ROOT" || value == "BASIC") && store_owner_organ.contains_key(&(x, y)) {
            root_basic_or_a_positions.push((x, y));
        }
    }
    
    root_basic_or_a_positions
}

//**************************************************************************
//TODO:
/*
- Check the available protein source
- Decide the proteins that I should get
- Check if the SPORER is need based on the position of the root and the protein
- 
*/
//**************************************************************************

// This function is responsible for finding all my root organs
fn root_organs_positions(
    store_positions_type: &HashMap<(i32, i32), String>,
    store_owner_organ: &HashMap<(i32, i32), i32>,

) -> Vec<(i32, i32)> {
    let mut root_organ_position: Vec<(i32, i32)> = Vec::new();
    for (&(x, y), value) in store_positions_type.iter() {
        if value == "ROOT" && store_owner_organ.contains_key(&(x, y)) {
            root_organ_position.push((x, y));
        }
    }
    
    root_organ_position
}

// This function return the direction to target
fn calculate_direction(current: (i32, i32), target: (i32, i32)) -> String {
    if current.0 == target.0 {
        if current.1 < target.1 {
            "S".to_string()
        } else {
            "N".to_string()
        }
    } else if current.0 < target.0 {
        "E".to_string()
    } else {
        "W".to_string()
    }
}


// THis will calculate the nearest organ to protein
fn find_nearest_organ(
    organ_position: &Vec<(i32, i32)>,
    protein_position: (i32, i32)
)-> i32 {
    let mut lowest_distance = std::i32::MAX;
    let mut position: (i32, i32) = (-1, -1);
    for &(x, y) in organ_position {
        let distance = manhattan_distance(protein_position, (x, y));
        if lowest_distance < distance {
            lowest_distance = distance;
            position = (x, y);
        }
    }

    lowest_distance
}

// This function will propose the protein that I should go to
fn proposed_proteins(
    store_positions_type: &HashMap<(i32, i32), String>,
    my_organ_position: &Vec<(i32, i32)>,
    opp_organ_position: &Vec<(i32, i32)>,
) -> Vec<(i32, i32)> {
    let mut proteins: Vec<(i32, i32)> = Vec::new();

    for (&(x, y), value) in store_positions_type.iter() {
        if value == "A" || value == "B" || value == "C" || value == "D" {
            if find_nearest_organ(my_organ_position, (x, y)) < find_nearest_organ(opp_organ_position, (x, y)) {
                proteins.push((x, y));
            }
        }
    }

    proteins
}



// This function will check the root of a specific organ
fn check_root_id(
    store_owner_organ: &HashMap<(i32, i32), i32>,
    organ_positon: (i32, i32)
) -> i32 {
    store_owner_organ.iter()
        .find_map(|(&key, &val)| {if key.0 == organ_positon.0 && key.1 == organ_positon.1 { Some(val) } else { None }})
        .unwrap_or(-1)
}

// Function that checks if the SPORER needed or not
fn is_sporer_needed(
    my_organ_position: &HashMap<(i32, i32), i32>,
    proposed_protein_position: &Vec<(i32, i32)>,
    width: i32,
    height: i32,
) -> bool {
    for &(protein_x, protein_y) in proposed_protein_position {
        for (&(organ_x, organ_y), _) in my_organ_position.iter() {
            // You can add some specific conditions to check the relationship 
            // between organ positions and protein positions here.
            // For example, if any organ is within a certain range from the protein
            // or if a specific type of protein requires a SPORER.
            if (protein_x - organ_x).abs() <= width / 2 && (protein_y - organ_y).abs() <= height / 2 {
                return true;
            }
        }
    }

    false
}


// This function is responsible for giving the id of a specific organ
fn search_for_organ_id(
    store_id_with_position: &HashMap<(i32, i32), i32>, 
    my_organ_position: &Vec<(i32, i32)>,
    search: (i32, i32)
) -> i32 {
    let mut closest_id = 0;
    let mut min_distance = std::i32::MAX;

    // Iterate over all positions in my_organ_position
    for &(x, y) in my_organ_position.iter() {
        let distance = (search.0 - x).abs() + (search.1 - y).abs(); // Manhattan distance

        // If this position is closer than previous ones, update closest_id
        if distance < min_distance {
            if let Some(&organ_id) = store_id_with_position.get(&(x, y)) {
                closest_id = organ_id;
                min_distance = distance;
            }
        }
    }

    closest_id
}

// Calculate Manhattan distance between two points
fn manhattan_distance(
    p1: (i32, i32), 
    p2: (i32, i32)
) -> i32 {
    (p1.0 - p2.0).abs() + (p1.1 - p2.1).abs()
}

// This function is responsible for proposing an attack on the opponenent
fn near_opponent(
    my_organ_position: &Vec<(i32, i32)>,
    store_positions_type: &HashMap<(i32, i32), String>,
    my_positions: (i32, i32)
) -> (bool, String) {
    
    for (&(x, y), value) in store_positions_type.iter() {
        if value == "BASIC" && !my_organ_position.contains(&(x, y)) {
            let distance = manhattan_distance(my_positions, (x, y));
            if distance == 1 {
                let direction = if x == my_positions.0 {
                    if y > my_positions.1 {
                        "S"
                    } else {
                        "N"
                    }
                } else {
                    if x > my_positions.0 {
                        "E"
                    } else {
                        "W"
                    }
                };
                return (true, direction.to_string());
            }
        }
    }

    (false, "".to_string())
}

// Function to find the next position towards "A" within a given range (max_distance = 5)
fn find_next_position_towards_a(
    my_organ_position: &Vec<(i32, i32)>,
    store_positions_type: &HashMap<(i32, i32), String>,
    proposed_protein_position: &Vec<(i32, i32)>,
    harvesting: Vec<(i32, i32)>,
    current_position: (i32, i32),
    max_distance: i32,
    width: i32,
    height: i32,
) -> Option<((i32, i32), i32, String, (i32, i32), bool)> {
    // Get all "A" positions
    let mut store_position: HashMap<(i32, i32), String> = HashMap::new();
    let attack = false;
    for (&(a_x, a_y), a_type) in store_positions_type {
        if !harvesting.contains(&(a_x, a_y)) {
            store_position.insert((a_x, a_y), a_type.clone());
        }
    }
    // let a_positions = search_for_a_positions(&store_position);
    let mut harvest: (i32, i32) = (-1, -1); 
    // eprintln!("a_positions: {:?}", a_positions);

    // Check for type A within range and find direction
    for &(a_x, a_y) in proposed_protein_position {
        let mut distance = manhattan_distance(current_position, (a_x, a_y));

        if distance <= max_distance && distance != 1 {
            eprintln!("that was the first condition");
            // Calculate the direction
            let direction = if a_x == current_position.0 {
                if a_y < current_position.1 {
                    "S"
                } else {
                    "N"
                }
            } else {
                if a_x > current_position.0 {
                    "E"
                } else {
                    "W"
                }
            };

            let directions = [(0, -1), (0, 1), (-1, 0), (1, 0)];
            distance = std::i32::MAX;
            let mut updated = false;
            let mut new_position = (0, 0);
            for &(dx, dy) in &directions {
                let new_x = current_position.0 + dx;
                let new_y = current_position.1 + dy;
                
                let new_distance = manhattan_distance((new_x, new_y), (a_x, a_y));
                if distance > new_distance && !store_positions_type.contains_key(&(new_x, new_y)) {
                    new_position = (new_x, new_y);
                    distance = new_distance;
                    updated = true;
                }
            }
            if updated {
                if distance == 1 {
                    harvest = (a_x, a_y);
                }
                eprintln!("that is weird {distance} {:?} {:?} {direction}", current_position, new_position);
                let check_opponent = near_opponent(my_organ_position, store_positions_type, new_position);
                if check_opponent.0 {
                    return Some((new_position, distance, check_opponent.1, harvest, check_opponent.0));
                }
                return Some((new_position, distance, direction.to_string(), harvest, attack));
            }
        }
    }

    // Fallback to default movement logic if no valid A positions within range
    if let Some((new_x, new_y)) = find_next_position_to_a(store_positions_type, current_position.0, current_position.1, width, height) {
        eprintln!("that was the second condition");
        let check_opponent = near_opponent(my_organ_position, store_positions_type, (new_x, new_y));
        if check_opponent.0 {
            return Some(((new_x, new_y), std::i32::MAX, check_opponent.1, harvest, check_opponent.0));
        }
        return Some(((new_x, new_y), std::i32::MAX, "".to_string(), harvest, attack));
    }

    for &(x, y) in my_organ_position.iter() {
        if let Some((new_x, new_y)) = find_next_position_to_a(store_positions_type, x, y, width, height) {
            eprintln!("that was the third condition");
            let check_opponent = near_opponent(my_organ_position, store_positions_type, (new_x, new_y));
            if check_opponent.0 {
                return Some(((new_x, new_y), std::i32::MAX, check_opponent.1, harvest, check_opponent.0));
            }
            return Some(((new_x, new_y), std::i32::MAX, "".to_string(), harvest, attack));
        }
    }

    None
}

// Existing function to search for "A" positions
fn search_for_a_positions(store_positions_type: &HashMap<(i32, i32), String>) -> Vec<(i32, i32)> {
    let mut a_positions = Vec::new();

    // Iterate over the HashMap and find positions where the value is "A"
    for (&(x, y), value) in store_positions_type.iter() {
        if value == "A" {
            a_positions.push((x, y));
        }
    }

    a_positions
}

// Existing function to find the next available position to move
fn find_next_position_to_a(
    store_positions_type: &HashMap<(i32, i32), String>,
    current_x: i32,
    current_y: i32,
    width: i32,
    height: i32,
) -> Option<(i32, i32)> {
    let directions = [(0, -1), (0, 1), (-1, 0), (1, 0)];

    // If no "A" positions found, fall back to searching for any available position
    for &(dx, dy) in &directions {
        let new_x = current_x + dx;
        let new_y = current_y + dy;
        let type_val = if let Some(v) = store_positions_type.get(&(new_x, new_y)) {
            v == "A"
        } else {
            false
        };
        eprintln!("the output of new_x: {new_x} and new_y:{new_y}");
        if new_x >= 0 && new_x < width && new_y >= 0 && new_y < height && !store_positions_type.contains_key(&(new_x, new_y)) && type_val != true {
            return Some((new_x, new_y));
        }
    }

    None
}


// Main game loop
fn main() {
    let mut input_line = String::new();
    io::stdin().read_line(&mut input_line).unwrap();
    let inputs = input_line.split(" ").collect::<Vec<_>>();
    let width = parse_input!(inputs[0], i32); // columns in the game grid
    let height = parse_input!(inputs[1], i32); // rows in the game grid
    let mut my_organ_position: Vec<(i32, i32)> = Vec::new();
    let mut opp_organ_position: Vec<(i32, i32)> = Vec::new();

    let mut store_owner_organ: HashMap<(i32, i32), i32> = HashMap::new();
    let mut actual_positions: HashMap<i32, (i32, i32)> = HashMap::new(); 
    let mut proposed_protein_position: Vec<(i32, i32)> = Vec::new();
    let mut turn = -1;

    let mut harvesting: Vec<(i32, i32)> = Vec::new();

    loop {

        let mut input_line = String::new();
        let mut store_positions_type: HashMap<(i32, i32), String> = HashMap::new();
        io::stdin().read_line(&mut input_line).unwrap();
        let entity_count = parse_input!(input_line, i32);
        let mut store_id_with_position: HashMap<(i32, i32), i32> = HashMap::new();

        for _ in 0..entity_count as usize {
            let mut input_line = String::new();
            io::stdin().read_line(&mut input_line).unwrap();
            let inputs = input_line.split(" ").collect::<Vec<_>>();
            let x = parse_input!(inputs[0], i32);
            let y = parse_input!(inputs[1], i32);
            let _type = inputs[2].trim().to_string();
            let owner = parse_input!(inputs[3], i32);

            
            let organ_id = parse_input!(inputs[4], i32);
            let organDir = inputs[5].trim().to_string();
            let organParentId = parse_input!(inputs[6], i32);
            let organRootId = parse_input!(inputs[7], i32);
            if owner == 1 {
                store_owner_organ.insert((x, y), organRootId );
                my_organ_position.push((x, y));
            }
            if owner == 0 {
                opp_organ_position.push((x, y));
            }
            store_positions_type.insert((x, y), _type);
            store_id_with_position.insert((x, y), organ_id);
        }

        if turn == -1 {
            let root_basic_or_a_positions = search_for_root_basic_or_a_in_owner(&store_positions_type, &store_owner_organ);
            if let Some(&(x, y)) = root_basic_or_a_positions.first() {
                let id = search_for_organ_id(&store_id_with_position, &my_organ_position, (x, y));
                if !actual_positions.contains_key(&id) {
                    actual_positions.insert(id, (x, y));
                } else {
                    actual_positions.entry(id).and_modify(|val| { val.0 = x; val.1 = y; }).or_insert((x, y));
                }
            }
            proposed_protein_position = proposed_proteins(&store_positions_type, &my_organ_position, &opp_organ_position);
            turn = 0;
        }
        // INPUT for my organism's status
        let mut input_line = String::new();
        io::stdin().read_line(&mut input_line).unwrap();
        let inputs = input_line.split(" ").collect::<Vec<_>>();
        let my_a = parse_input!(inputs[0], i32);
        let my_b = parse_input!(inputs[1], i32);
        let my_c = parse_input!(inputs[2], i32);
        let my_d = parse_input!(inputs[3], i32); // your protein stock

        let mut input_line = String::new();
        io::stdin().read_line(&mut input_line).unwrap();
        let inputs = input_line.split(" ").collect::<Vec<_>>();
        let opp_a = parse_input!(inputs[0], i32);
        let opp_b = parse_input!(inputs[1], i32);
        let opp_c = parse_input!(inputs[2], i32);
        let opp_d = parse_input!(inputs[3], i32); // opponent's protein stock

        let mut input_line = String::new();
        io::stdin().read_line(&mut input_line).unwrap();
        let required_actions_count = parse_input!(input_line, i32);

        // for _ in 0..required_actions_count as usize {
        for (&id, &(target_x, target_y)) in actual_positions.clone().iter() {
            if let Some(((new_x, new_y), distance, direction, harvest, attack)) = find_next_position_towards_a(
                &my_organ_position,
                &store_positions_type,
                &proposed_protein_position,
                harvesting.clone(),
                (target_x, target_y),
                5,
                width,
                height,
            ) {
                let id_2 = search_for_organ_id(&store_id_with_position, &my_organ_position, (target_x, target_y));
                if attack {
                    // It will throw an TENTACLE command to attack nearbe
                    actual_positions.entry(id).and_modify(|val| { val.0 = new_x; val.1 = new_y; }).or_insert((new_x, new_y));
                    println!("GROW {} {} {} TENTACLE {}", id_2, new_x, new_y, direction);
                }
                else if distance == 1 {
                    // Print special grow command when near "A"
                    actual_positions.entry(id).and_modify(|val| { val.0 = new_x; val.1 = new_y; }).or_insert((new_x, new_y));
                    harvesting.push(harvest);
                    println!("GROW {} {} {} HARVESTER {}", id_2, new_x, new_y, direction);
                } else {
                    actual_positions.entry(id).and_modify(|val| { val.0 = new_x; val.1 = new_y; }).or_insert((new_x, new_y));
                    println!("GROW {} {} {} BASIC", id_2, new_x, new_y);
                }
            } else {
                println!("WAIT");
            }
        }
    }
}
